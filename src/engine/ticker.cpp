//
//  Lol Engine
//
//  Copyright © 2010—2019 Sam Hocevar <sam@hocevar.net>
//
//  Lol Engine is free software. It comes without any warranty, to
//  the extent permitted by applicable law. You can redistribute it
//  and/or modify it under the terms of the Do What the Fuck You Want
//  to Public License, Version 2, as published by the WTFPL Task Force.
//  See http://www.wtfpl.net/ for more details.
//

#include <lol/engine-internal.h>

#include <cstdlib>
#include <cstdint>
#include <functional>

namespace lol
{

/*
 * Ticker implementation class
 */

class ticker_data
{
    friend class ticker;

public:
    ticker_data()
    {
        if (has_threads())
        {
            gamethread = std::make_unique<thread>(std::bind(&ticker_data::GameThreadMain, this));
            drawtick.push(1);

            diskthread = std::make_unique<thread>(std::bind(&ticker_data::DiskThreadMain, this));
        }
    }

    ~ticker_data()
    {
        ASSERT(DEPRECATED_nentities == 0,
               "still %d entities in ticker\n", DEPRECATED_nentities);
        ASSERT(DEPRECATED_m_autolist.count() == 0,
               "still %d autoreleased entities\n", DEPRECATED_m_autolist.count());
        msg::debug("%d frames required to quit\n", m_frame - m_quitframe);

        if (has_threads())
        {
            gametick.push(0);
            disktick.push(0);
            gamethread.release();
            diskthread.release();
            ASSERT(drawtick.size() == 0);
        }
    }

    void handle_shutdown();
    void collect_garbage();

private:
    // Tickables waiting to be inserted
    queue<std::shared_ptr<tickable>> m_todo;

    std::unordered_set<std::shared_ptr<tickable>> m_tickables;

    /* Entity management */
    array<entity *> DEPRECATED_m_todolist, DEPRECATED_m_todolist_delayed, DEPRECATED_m_autolist;
    array<entity *> DEPRECATED_m_list[(int)tickable::group::all::end];
    array<int> DEPRECATED_m_scenes[(int)tickable::group::all::end];
    int DEPRECATED_nentities = 0;

    /* Fixed framerate management */
    int m_frame = 0, m_recording = 0;
    timer m_timer;
    float deltatime = 0.f, bias = 0.f, fps = 0.f;
#if LOL_BUILD_DEBUG
    float keepalive = 0;
#endif

    /* The three main functions (for now) */
    static void GameThreadTick();
    static void DrawThreadTick();
    static void DiskThreadTick();

    /* The associated background threads */
    void GameThreadMain();
    void DrawThreadMain(); /* unused for now */
    void DiskThreadMain();
    std::unique_ptr<thread> gamethread, diskthread;
    queue<int> gametick, drawtick, disktick;

    /* Shutdown management */
    int m_quit = 0, m_quitframe = 0, m_quitdelay = 20, m_panic = 0;
};

static std::unique_ptr<ticker_data> data;

//
// Add/remove tickable objects
//

void ticker::add(std::shared_ptr<tickable> entity)
{
    data->m_tickables.insert(entity);
}

void ticker::remove(std::shared_ptr<tickable> entity)
{
    //weak_ptr<tickable> p = entity;
    data->m_tickables.erase(entity);
}

//
// Old API for entities
//

void Ticker::Register(entity *entity)
{
    /* If we are called from its constructor, the object's vtable is not
     * ready yet, so we do not know which group this entity belongs to. Wait
     * until the first tick. */
    data->DEPRECATED_m_todolist_delayed.push(entity);

    /* Objects are autoreleased by default. Put them in a list. */
    data->DEPRECATED_m_autolist.push(entity);
    entity->add_flags(entity::flags::autorelease);
    entity->m_ref = 1;

    data->DEPRECATED_nentities++;
}

void Ticker::Ref(entity *entity)
{
    ASSERT(entity, "dereferencing nullptr entity\n");
    ASSERT(!entity->has_flags(entity::flags::destroying),
           "referencing entity scheduled for destruction %s\n",
           entity->GetName().c_str());

    if (entity->has_flags(entity::flags::autorelease))
    {
        /* Get the entity out of the autorelease list. This is usually
         * very fast since the last entry in autolist is the last
         * registered entity. */
        for (int i = data->DEPRECATED_m_autolist.count(); i--; )
        {
            if (data->DEPRECATED_m_autolist[i] == entity)
            {
                data->DEPRECATED_m_autolist.remove_swap(i);
                break;
            }
        }
        entity->remove_flags(entity::flags::autorelease);
    }
    else
        entity->m_ref++;
}

int Ticker::Unref(entity *entity)
{
    ASSERT(entity, "dereferencing null entity\n");
    ASSERT(entity->m_ref > 0,
           "dereferencing unreferenced entity %s\n", entity->GetName().c_str());
    ASSERT(!entity->has_flags(entity::flags::autorelease),
           "dereferencing autoreleased entity %s\n", entity->GetName().c_str());

    return --entity->m_ref;
}

void ticker_data::GameThreadMain()
{
#if LOL_BUILD_DEBUG
    msg::debug("ticker game thread initialised\n");
#endif

    for (;;)
    {
        int tick = gametick.pop();
        if (!tick)
            break;

        GameThreadTick();

        drawtick.push(1);
    }

    drawtick.push(0);

#if LOL_BUILD_DEBUG
    msg::debug("ticker game thread terminated\n");
#endif
}

void ticker_data::DrawThreadMain() /* unused */
{
#if LOL_BUILD_DEBUG
    msg::debug("ticker draw thread initialised\n");
#endif

    for (;;)
    {
        int tick = drawtick.pop();
        if (!tick)
            break;

        DrawThreadTick();

        gametick.push(1);
    }

#if LOL_BUILD_DEBUG
    msg::debug("ticker draw thread terminated\n");
#endif
}

void ticker_data::DiskThreadMain()
{
    /* FIXME: temporary hack to avoid crashes on the PS3 */
    disktick.pop();
}

//-----------------------------------------------------------------------------
void ticker_data::GameThreadTick()
{
    Profiler::Stop(Profiler::STAT_TICK_FRAME);
    Profiler::Start(Profiler::STAT_TICK_FRAME);

    Profiler::Start(Profiler::STAT_TICK_GAME);

#if 0
    msg::debug("-------------------------------------\n");
    for (int g = 0; g < (int)tickable::group::all::end; ++g)
    {
        msg::debug("%s Group %d\n",
                   (g < (int)tickable::group::game::end) ? "Game" : "Draw", g);

        for (int i = 0; i < data->DEPRECATED_m_list[g].count(); ++i)
        {
            entity *e = data->DEPRECATED_m_list[g][i];
            msg::debug("  \\-- [%p] %s (m_ref %d, destroy %d)\n",
                       e, e->GetName().c_str(), e->m_ref, e->has_flags(entity::flags::destroying));
        }
    }
#endif

    data->m_frame++;

    /* Ensure some randomness */
    (void)rand<int>();

    /* If recording with fixed framerate, set deltatime to a fixed value */
    if (data->m_recording && data->fps)
    {
        data->deltatime = 1.f / data->fps;
    }
    else
    {
        data->deltatime = data->m_timer.get();
        data->bias += data->deltatime;
    }

    /* Do not go below 15 fps */
    if (data->deltatime > 1.f / 15.f)
    {
        data->deltatime = 1.f / 15.f;
        data->bias = 0.f;
    }

#if LOL_BUILD_DEBUG
    data->keepalive += data->deltatime;
    if (data->keepalive > 10.f)
    {
        msg::debug("ticker keepalive: tick!\n");
        data->keepalive = 0.f;
    }
#endif

    data->handle_shutdown();
    data->collect_garbage();

    /* Insert waiting objects into the appropriate lists */
    while (data->DEPRECATED_m_todolist.count())
    {
        entity *e = data->DEPRECATED_m_todolist.last();

        //If the entity has no mask, default it
        if (e->m_scene_mask == 0)
        {
            Scene::GetScene().Link(e);
        }

        data->DEPRECATED_m_todolist.remove(-1);
        data->DEPRECATED_m_list[(int)e->m_gamegroup].push(e);
        if (e->m_drawgroup != tickable::group::draw::none)
        {
            if (data->DEPRECATED_m_scenes[(int)e->m_drawgroup].count() < Scene::GetCount())
                data->DEPRECATED_m_scenes[(int)e->m_drawgroup].resize(Scene::GetCount());

            int added_count = 0;
            for (int i = 0; i < Scene::GetCount(); i++)
            {
                //If entity is concerned by this scene, add it in the list
                if (Scene::GetScene(i).IsRelevant(e))
                {
                    data->DEPRECATED_m_list[(int)e->m_drawgroup].insert(e, data->DEPRECATED_m_scenes[(int)e->m_drawgroup][i]);
                    added_count++;
                }
                //Update scene index
                data->DEPRECATED_m_scenes[(int)e->m_drawgroup][i] += added_count;
            }
        }
    }

    data->DEPRECATED_m_todolist = data->DEPRECATED_m_todolist_delayed;
    data->DEPRECATED_m_todolist_delayed.clear();

    for (int g = (int)tickable::group::game::begin; g < (int)tickable::group::game::end; ++g)
    {
        for (int i = 0; i < data->DEPRECATED_m_list[g].count(); ++i)
        {
            entity *e = data->DEPRECATED_m_list[g][i];

            if (!e->has_flags(entity::flags::init_game))
            {
                // Ensure the entity is initialised on the game thread
                if (e->init_game())
                    e->add_flags(entity::flags::init_game);
            }
            else if (e->has_flags(entity::flags::destroying))
            {
                // If entity is being destroyed, call the release code
                if (!e->has_flags(entity::flags::release_game) && e->release_game())
                    e->add_flags(entity::flags::release_game);
            }
        }
    }

    /* Tick objects for the game loop */
    for (int g = (int)tickable::group::game::begin; g < (int)tickable::group::game::end && !data->m_quit /* Stop as soon as required */; ++g)
    {
        for (int i = 0; i < data->DEPRECATED_m_list[g].count() && !data->m_quit /* Stop as soon as required */; ++i)
        {
            entity *e = data->DEPRECATED_m_list[g][i];

            if (e->has_flags(entity::flags::init_game)
                 && !e->has_flags(entity::flags::destroying))
            {
#if !LOL_BUILD_RELEASE
                if (e->m_tickstate != tickable::state::idle)
                    msg::error("entity %s [%p] not idle for game tick\n",
                               e->GetName().c_str(), e);
                e->m_tickstate = tickable::state::pre_game;
#endif
                e->tick_game(data->deltatime);
#if !LOL_BUILD_RELEASE
                if (e->m_tickstate != tickable::state::post_game)
                    msg::error("entity %s [%p] missed super game tick\n",
                               e->GetName().c_str(), e);
                e->m_tickstate = tickable::state::idle;
#endif
            }
        }
    }

    Profiler::Stop(Profiler::STAT_TICK_GAME);
}

//-----------------------------------------------------------------------------
void ticker_data::DrawThreadTick()
{
    Profiler::Start(Profiler::STAT_TICK_DRAW);

    for (int g = (int)tickable::group::draw::begin; g < (int)tickable::group::draw::end; ++g)
    {
        for (int i = 0; i < data->DEPRECATED_m_list[g].count(); ++i)
        {
            entity *e = data->DEPRECATED_m_list[g][i];

            if (!e->has_flags(entity::flags::init_draw))
            {
                // Ensure the entity is initialised on the draw thread
                if (e->init_draw())
                    e->add_flags(entity::flags::init_draw);
            }
            else if (e->has_flags(entity::flags::destroying))
            {
                // If entity is being destroyed, call the release code
                if (!e->has_flags(entity::flags::release_draw) && e->release_draw())
                    e->add_flags(entity::flags::release_draw);
            }
        }
    }

    /* Render each scene one after the other */
    for (int idx = 0; idx < Scene::GetCount() && !data->m_quit /* Stop as soon as required */; ++idx)
    {
        Scene& scene = Scene::GetScene(idx);

        /* Enable display */
        scene.EnableDisplay();

        scene.pre_render(data->deltatime);

        /* Tick objects for the draw loop */
        for (int g = (int)tickable::group::draw::begin; g < (int)tickable::group::draw::end && !data->m_quit /* Stop as soon as required */; ++g)
        {
            switch (g)
            {
            case (int)tickable::group::draw::begin:
                scene.Reset();
                break;
            default:
                break;
            }

            for (int i = 0; i < data->DEPRECATED_m_list[g].count() && !data->m_quit /* Stop as soon as required */; ++i)
            {
                entity *e = data->DEPRECATED_m_list[g][i];

                if (e->has_flags(entity::flags::init_draw)
                     && !e->has_flags(entity::flags::destroying))
                {
#if !LOL_BUILD_RELEASE
                    if (e->m_tickstate != tickable::state::idle)
                        msg::error("entity %s [%p] not idle for draw tick\n",
                                   e->GetName().c_str(), e);
                    e->m_tickstate = tickable::state::pre_draw;
#endif
                    e->tick_draw(data->deltatime, scene);
#if !LOL_BUILD_RELEASE
                    if (e->m_tickstate != tickable::state::post_draw)
                        msg::error("entity %s [%p] missed super draw tick\n",
                                   e->GetName().c_str(), e);
                    e->m_tickstate = tickable::state::idle;
#endif
                }
            }
        }

        /* Do the render step */
        scene.render(data->deltatime);

        scene.post_render(data->deltatime);

        /* Disable display */
        scene.DisableDisplay();
    }

    Profiler::Stop(Profiler::STAT_TICK_DRAW);
}

// If shutdown is stuck, kick the first entity we meet and see
// whether it makes things better. Note that it is always a bug to
// have referenced entities after 20 frames, but at least this
// safeguard makes it possible to exit the program cleanly.
void ticker_data::handle_shutdown()
{
    if (m_quit && !((m_frame - m_quitframe) % m_quitdelay))
    {
        int n = 0;
        m_panic = 2 * (m_panic + 1);

        for (int g = 0; g < (int)tickable::group::all::end && n < m_panic; ++g)
        for (int i = 0; i < DEPRECATED_m_list[g].count() && n < m_panic; ++i)
        {
            entity * e = DEPRECATED_m_list[g][i];
            if (e->m_ref)
            {
#if !LOL_BUILD_RELEASE
                msg::error("poking %s\n", e->GetName().c_str());
#endif
                e->m_ref--;
                n++;
            }
        }

#if !LOL_BUILD_RELEASE
        if (n)
            msg::error("%d entities stuck after %d frames, poked %d\n",
                       DEPRECATED_nentities, m_quitdelay, n);
#endif

        m_quitdelay = m_quitdelay > 1 ? m_quitdelay / 2 : 1;
    }
}

void ticker_data::collect_garbage()
{
    /* Garbage collect objects that can be destroyed. We can do this
     * before inserting awaiting objects, because only objects already
     * in the tick lists can be marked for destruction. */
    array<entity*> destroy_list;
    for (int g = 0; g < (int)tickable::group::all::end; ++g)
    {
        for (int i = DEPRECATED_m_list[g].count(); i--;)
        {
            entity *e = DEPRECATED_m_list[g][i];

            if (!e->has_flags(entity::flags::destroying))
            {
                if (e->m_ref <= 0 && g >= (int)tickable::group::draw::begin)
                    e->add_flags(entity::flags::destroying);
                continue;
            }

            // If entity is being destroyed but not released yet, retry later.
            if (!e->has_flags(entity::flags::release_game)
                 || !e->has_flags(entity::flags::release_draw))
                continue;

            // If entity is to be destroyed, remove it.
            DEPRECATED_m_list[g].remove_swap(i);

            // Draw group specific logic
            if (g >= (int)tickable::group::game::end)
            {
                int removal_count = 0;
                for (int j = 0; j < Scene::GetCount(); j++)
                {
                    // If entity is concerned by this scene, add it in the list
                    if (Scene::GetScene(j).IsRelevant(e))
                        removal_count++;
                    // Update scene index
                    DEPRECATED_m_scenes[(int)e->m_drawgroup][j] -= removal_count;
                }
            }

            destroy_list.push_unique(e);
        }
    }

    if (!!destroy_list.count())
    {
        DEPRECATED_nentities -= destroy_list.count();
        for (entity* e : destroy_list)
            delete e;
    }
}

void ticker_data::DiskThreadTick()
{
    ;
}

void Ticker::SetState(entity * /* entity */, uint32_t /* state */)
{

}

void Ticker::SetStateWhenMatch(entity * /* entity */, uint32_t /* state */,
                               entity * /* other_entity */, uint32_t /* other_state */)
{

}

void ticker::setup(float fps)
{
    data = std::make_unique<ticker_data>();
    data->fps = fps;
}

void ticker::teardown()
{
    data.release();
}

void ticker::tick_draw()
{
    if (has_threads())
    {
        int n = data->drawtick.pop();
        if (n == 0)
            return;
    }
    else
        ticker_data::GameThreadTick();

    ticker_data::DrawThreadTick();

    Profiler::Start(Profiler::STAT_TICK_BLIT);

    /* Signal game thread that it can carry on */
    if (has_threads())
        data->gametick.push(1);
    else
        ticker_data::DiskThreadTick();

    /* Clamp FPS */
    Profiler::Stop(Profiler::STAT_TICK_BLIT);

#if !__EMSCRIPTEN__
    /* If framerate is fixed, force wait time to 1/FPS. Otherwise, set wait
     * time to 0. */
    float frametime = data->fps ? 1.f / data->fps : 0.f;

    if (frametime > data->bias + .2f)
        frametime = data->bias + .2f; /* Don't go below 5 fps */
    if (frametime > data->bias)
        data->m_timer.wait(frametime - data->bias);

    /* If recording, do not try to compensate for lag. */
    if (!data->m_recording)
        data->bias -= frametime;
#endif
}

void Ticker::StartRecording()
{
    ++data->m_recording;
}

void Ticker::StopRecording()
{
    --data->m_recording;
}

int Ticker::GetFrameNum()
{
    return data->m_frame;
}

void Ticker::Shutdown()
{
    /* We're bailing out. Release all autorelease objects. */
    while (data->DEPRECATED_m_autolist.count())
    {
        data->DEPRECATED_m_autolist.last()->m_ref--;
        data->DEPRECATED_m_autolist.remove(-1);
    }

    data->m_quit = 1;
    data->m_quitframe = data->m_frame;
}

int Ticker::Finished()
{
    return !data->DEPRECATED_nentities;
}

} /* namespace lol */

