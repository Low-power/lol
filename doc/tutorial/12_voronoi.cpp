//
//  Lol Engine — Voronoi diagram tutorial
//
//  Copyright © 2015—2019 Sam Hocevar <sam@hocevar.net>
//            © 2011—2015 Benjamin “Touky” Huet <huet.benjamin@gmail.com>
//
//  Lol Engine is free software. It comes without any warranty, to
//  the extent permitted by applicable law. You can redistribute it
//  and/or modify it under the terms of the Do What the Fuck You Want
//  to Public License, Version 2, as published by the WTFPL Task Force.
//  See http://www.wtfpl.net/ for more details.
//

#if HAVE_CONFIG_H
#   include "config.h"
#endif

#include <memory>

#include <lol/engine.h>
#include "loldebug.h"

using namespace lol;

LOLFX_RESOURCE_DECLARE(12_voronoi);
LOLFX_RESOURCE_DECLARE(12_voronoi_setup);
LOLFX_RESOURCE_DECLARE(12_voronoi_distance);
LOLFX_RESOURCE_DECLARE(12_distance);
LOLFX_RESOURCE_DECLARE(12_texture_to_screen);

enum FboType
{
    SrcVoronoiFbo,
    VoronoiFbo,
    DistanceVoronoiFbo,
    DistanceFbo,

    MaxFboType
};

class Voronoi : public WorldEntity
{
public:
    Voronoi()
    {
        m_vertices << vec2( 1.0,  1.0);
        m_vertices << vec2(-1.0, -1.0);
        m_vertices << vec2( 1.0, -1.0);
        m_vertices << vec2(-1.0, -1.0);
        m_vertices << vec2( 1.0,  1.0);
        m_vertices << vec2(-1.0,  1.0);
        m_ready = false;
        m_cur_fbo = 0;
        m_time = .0f;
        m_timer = -1.0f;
        mode = 0;
    }

    virtual void tick_game(float seconds)
    {
        WorldEntity::tick_game(seconds);

        auto keyboard = input::keyboard();

        // Shutdown logic
        if (keyboard->key_released(input::key::SC_Escape))
            Ticker::Shutdown();

        m_time += seconds;
        m_hotspot = 0.4f * vec3((float)lol::sin(m_time * 4.0) + (float)lol::cos(m_time * 5.3),
                                (float)lol::sin(m_time * 5.7) + (float)lol::cos(m_time * 4.4),
                                (float)lol::sin(m_time * 5.0));
        m_color = 0.25f * vec3(1.1f + (float)lol::sin(m_time * 2.5 + 1.0),
                               1.1f + (float)lol::sin(m_time * 2.8 + 1.3),
                               1.1f + (float)lol::sin(m_time * 2.7));
        /* Saturate dot color */
        float x = std::max(m_color.x, std::max(m_color.y, m_color.z));
        m_color /= x;
    }

    virtual void tick_draw(float seconds, Scene &scene)
    {
        WorldEntity::tick_draw(seconds, scene);

        if (!m_ready)
        {
            m_vdecl = std::make_shared<VertexDeclaration>(VertexStream<vec2>(VertexUsage::Position));

            m_vbo = std::make_shared<VertexBuffer>(m_vertices.bytes());
            m_vbo->set_data(m_vertices.data(), m_vertices.bytes());

            m_screen_shader = Shader::Create(LOLFX_RESOURCE_NAME(12_texture_to_screen));
            m_screen_coord = m_screen_shader->GetAttribLocation(VertexUsage::Position, 0);
            m_screen_texture = m_screen_shader->GetUniformLocation("u_texture");

            for (int i = 0; i < MaxFboType; ++i)
            {
                m_fbos.push(std::make_shared<Framebuffer>(Video::GetSize()), 0, array<ShaderUniform>(), array<ShaderAttrib>() );

                if (i == SrcVoronoiFbo)
                {
                    m_fbos[i].m2 = Shader::Create(LOLFX_RESOURCE_NAME(12_voronoi_setup));
                    m_fbos[i].m3 << m_fbos[i].m2->GetUniformLocation("u_texture");
                    m_fbos[i].m3 << m_fbos[i].m2->GetUniformLocation("u_source_point");
                    m_fbos[i].m3 << m_fbos[i].m2->GetUniformLocation("u_screen_res");
                    m_fbos[i].m4 << m_fbos[i].m2->GetAttribLocation(VertexUsage::Position, 0);
                }
                else if (i == VoronoiFbo)
                {
                    m_fbos[i].m2 = Shader::Create(LOLFX_RESOURCE_NAME(12_voronoi));
                    m_fbos[i].m3 << m_fbos[i].m2->GetUniformLocation("u_texture");
                    m_fbos[i].m3 << m_fbos[i].m2->GetUniformLocation("u_step");
                    m_fbos[i].m3 << m_fbos[i].m2->GetUniformLocation("u_screen_res");
                    m_fbos[i].m4 << m_fbos[i].m2->GetAttribLocation(VertexUsage::Position, 0);
                }
                else if (i == DistanceVoronoiFbo)
                {
                    m_fbos[i].m2 = Shader::Create(LOLFX_RESOURCE_NAME(12_voronoi_distance));
                }
                else if (i == DistanceFbo)
                {
                    m_fbos[i].m2 = Shader::Create(LOLFX_RESOURCE_NAME(12_distance));
                }

                m_fbos.last().m1->Bind();
                {
                    render_context rc(scene.get_renderer());
                    rc.clear_color(vec4(0.f, 0.f, 0.f, 1.f));
                    rc.clear_depth(1.f);
                    scene.get_renderer()->clear(ClearMask::Color | ClearMask::Depth);
                }
                m_fbos.last().m1->Unbind();
            }

            temp_buffer = std::make_shared<Framebuffer>(Video::GetSize());
            temp_buffer->Bind();
            {
                render_context rc(scene.get_renderer());
                rc.clear_color(vec4(0.f, 0.f, 0.f, 1.f));
                rc.clear_depth(1.f);
                scene.get_renderer()->clear(ClearMask::Color | ClearMask::Depth);
            }
            temp_buffer->Unbind();

            m_ready = true;
            /* FIXME: this object never cleans up */

            //SRC SETUP
            m_cur_fbo = VoronoiFbo;
        }

        auto keyboard = input::keyboard();

        if (keyboard->key_released(input::key::SC_O))
            voronoi_points.pop();
        else if (keyboard->key_released(input::key::SC_P))
            voronoi_points.push(vec3(rand<float>(512.f), rand<float>(512.f), .0f),
                    vec2(64.f + rand<float>(64.f), 64.f + rand<float>(64.f)));
        else if (keyboard->key_released(input::key::SC_F1))
            m_cur_fbo = SrcVoronoiFbo;
        else if (keyboard->key_released(input::key::SC_F2))
            m_cur_fbo = VoronoiFbo;
        else if (keyboard->key_released(input::key::SC_F3))
        {
            voronoi_points.clear();
            if (mode == 0)
            {
                int i = 4;
                while (i-- > 0)
                    voronoi_points.push(vec3(rand<float>(512.f), rand<float>(512.f), .0f),
                                        vec2(64.f + rand<float>(64.f), 64.f + rand<float>(64.f))
                                        //vec2::zero
                                        );
                mode = 1;
            }
            else
            {
                mode = 0;
            }
        }

        if (mode == 0)
        {
            voronoi_points.clear();
            int maxi = 6;
            for (int i = 0; i < maxi; ++i)
            {
                float mi = (float)maxi;
                float j = (float)i;
                float f_time = (float)m_time;
                voronoi_points.push(vec3(256.f) + 196.f * vec3(lol::cos( f_time + j * 2.f * F_PI / mi), lol::sin( f_time + j * 2.f * F_PI / mi), .0f), vec2(.0f));
                voronoi_points.push(vec3(256.f) + 128.f * vec3(lol::cos(-f_time + j * 2.f * F_PI / mi), lol::sin(-f_time + j * 2.f * F_PI / mi), .0f), vec2(.0f));
                voronoi_points.push(vec3(256.f) +  64.f * vec3(lol::cos( f_time + j * 2.f * F_PI / mi), lol::sin( f_time + j * 2.f * F_PI / mi), .0f), vec2(.0f));
                voronoi_points.push(vec3(256.f) +  32.f * vec3(lol::cos(-f_time + j * 2.f * F_PI / mi), lol::sin(-f_time + j * 2.f * F_PI / mi), .0f), vec2(.0f));
            }
            voronoi_points.push(vec3(256.f), vec2(0.f));
        }

        temp_buffer->Bind();
        {
            render_context rc(scene.get_renderer());
            rc.clear_color(vec4(0.f, 0.f, 0.f, 1.f));
            rc.clear_depth(1.f);
            scene.get_renderer()->clear(ClearMask::Color | ClearMask::Depth);
        }
        temp_buffer->Unbind();

        {
            vec2 limit(1.f, 511.f);
            //SRC SETUP
            for (int j = 0; j < voronoi_points.count(); ++j)
            {
                voronoi_points[j].m1 = vec3(voronoi_points[j].m1.xy + voronoi_points[j].m2 * seconds, voronoi_points[j].m1.z);
                if (voronoi_points[j].m1.x >= limit.y || voronoi_points[j].m1.x <= limit.x)
                {
                    voronoi_points[j].m2.x *= -1.f;
                    voronoi_points[j].m1.x = clamp(voronoi_points[j].m1.x, limit.x, limit.y);
                }
                if (voronoi_points[j].m1.y >= limit.y || voronoi_points[j].m1.y <= limit.x)
                {
                    voronoi_points[j].m2.y *= -1.f;
                    voronoi_points[j].m1.y = clamp(voronoi_points[j].m1.y, limit.x, limit.y);
                }
                voronoi_points[j].m1.z = ((float)j + 1) / ((float)voronoi_points.count());
            }

            int f = SrcVoronoiFbo;

            m_fbos[f].m1->Bind();
            {
                render_context rc(scene.get_renderer());
                rc.clear_color(vec4(0.f, 0.f, 0.f, 1.f));
                rc.clear_depth(1.f);
                scene.get_renderer()->clear(ClearMask::Color | ClearMask::Depth);
            }
            m_fbos[f].m1->Unbind();

            int buf = voronoi_points.count() % 2;
            for (int j = 0; j < voronoi_points.count(); ++j)
            {
                std::shared_ptr<Framebuffer> dst_buf, src_buf;

                if (buf)
                {
                    dst_buf = m_fbos[f].m1;
                    src_buf = temp_buffer;
                }
                else
                {
                    src_buf = m_fbos[f].m1;
                    dst_buf = temp_buffer;
                }

                dst_buf->Bind();
                /* FIXME: we should just disable depth test in the shader */
                scene.get_renderer()->clear(ClearMask::Depth);
                m_fbos[f].m2->Bind();

                int i = 0;
                m_fbos[f].m2->SetUniform(m_fbos[f].m3[i++], src_buf->GetTextureUniform(), 0); //"u_texture"
                m_fbos[f].m2->SetUniform(m_fbos[f].m3[i++], voronoi_points[j].m1); //"u_source_point"
                m_fbos[f].m2->SetUniform(m_fbos[f].m3[i++], vec2(512.f, 512.f)); //"u_screen_res"

                m_vdecl->Bind();
                m_vdecl->SetStream(m_vbo, m_fbos[f].m4.last());
                m_vdecl->DrawElements(MeshPrimitive::Triangles, 0, 6);
                m_vdecl->Unbind();
                m_fbos[f].m2->Unbind();
                dst_buf->Unbind();

                buf = 1 - buf;
            }
        }

        scene.get_renderer()->clear(ClearMask::Color | ClearMask::Depth);

        //FRAME BUFFER DRAW
        m_timer -= seconds;
        if (m_timer < .0f && m_cur_fbo != SrcVoronoiFbo)
        {
            //m_timer = 1.0f;
            m_fbos[m_cur_fbo].m1->Bind();
            {
                render_context rc(scene.get_renderer());
                rc.clear_color(vec4(0.f, 0.f, 0.f, 1.f));
                rc.clear_depth(1.f);
                scene.get_renderer()->clear(ClearMask::Color | ClearMask::Depth);
            }
            m_fbos[m_cur_fbo].m1->Unbind();

            ivec2 curres = ivec2(512, 512) / 2;
            int buf = 0;
            while (1)
            {
                std::shared_ptr<Framebuffer> dst_buf, src_buf;
                std::shared_ptr<Shader> shader;

                if (curres == ivec2::zero)
                    shader = m_screen_shader;
                else
                    shader = m_fbos[m_cur_fbo].m2;

                if (curres.x == 256)
                    src_buf = m_fbos[SrcVoronoiFbo].m1;
                else if (buf)
                    src_buf = m_fbos[m_cur_fbo].m1;
                else
                    src_buf = temp_buffer;

                if (buf)
                    dst_buf = temp_buffer;
                else
                    dst_buf = m_fbos[m_cur_fbo].m1;

                dst_buf->Bind();
                /* FIXME: we should just disable depth test in the shader */
                scene.get_renderer()->clear(ClearMask::Depth);
                shader->Bind();

                int i = 0;
                if (curres == ivec2::zero)
                    m_screen_shader->SetUniform(m_screen_texture, src_buf->GetTextureUniform(), 0);
                else if (m_cur_fbo == VoronoiFbo)
                {
                    shader->SetUniform(m_fbos[m_cur_fbo].m3[i++], src_buf->GetTextureUniform(), 0); //"u_texture"
                    shader->SetUniform(m_fbos[m_cur_fbo].m3[i++], ((float)curres.x) / 512.f); //"u_step"
                    shader->SetUniform(m_fbos[m_cur_fbo].m3[i++], vec2(512.f, 512.f)); //"u_screen_res"
                }

                m_vdecl->Bind();
                m_vdecl->SetStream(m_vbo, m_fbos[m_cur_fbo].m4.last());
                m_vdecl->DrawElements(MeshPrimitive::Triangles, 0, 6);
                m_vdecl->Unbind();
                m_fbos[m_cur_fbo].m2->Unbind();
                dst_buf->Unbind();

                if (curres == ivec2::zero)
                    break;
                if (curres == ivec2(1))
                {
                    if (buf == 1)
                        curres = ivec2::zero;
                    else
                        break;
                }
                buf = 1 - buf;
                curres /= 2;
            }
        }

        //SCREEN DRAW
        m_screen_shader->Bind();
        m_screen_shader->SetUniform(m_screen_texture, m_fbos[m_cur_fbo].m1->GetTextureUniform(), 0);
        m_vdecl->Bind();
        m_vdecl->SetStream(m_vbo, m_screen_coord);
        m_vdecl->DrawElements(MeshPrimitive::Triangles, 0, 6);
        m_vdecl->Unbind();
        m_screen_shader->Unbind();
    }

private:
    array<vec3, vec2> voronoi_points;
    array<vec2> m_vertices;
    std::shared_ptr<Shader> m_screen_shader;
    ShaderAttrib m_screen_coord;
    ShaderUniform m_screen_texture;

    std::shared_ptr<VertexDeclaration> m_vdecl;
    std::shared_ptr<VertexBuffer> m_vbo;

    array<std::shared_ptr<Framebuffer>, std::shared_ptr<Shader>, array<ShaderUniform>, array<ShaderAttrib> > m_fbos;
    std::shared_ptr<Framebuffer> temp_buffer;

    int mode;
    int m_cur_fbo;
    double m_time;
    vec3 m_hotspot, m_color;
    bool m_ready;
    float m_timer;
};

int main(int argc, char **argv)
{
    sys::init(argc, argv);

    Application app("Tutorial 12: Jump Flooding Algorithm & Voronoi", ivec2(512, 512), 60.0f);

    new Voronoi();

    app.Run();
    return EXIT_SUCCESS;
}

