//
// Lol Engine - Framebuffer Object tutorial
//
// Copyright: (c) 2012 Sam Hocevar <sam@hocevar.net>
//   This program is free software; you can redistribute it and/or
//   modify it under the terms of the Do What The Fuck You Want To
//   Public License, Version 2, as published by Sam Hocevar. See
//   http://sam.zoy.org/projects/COPYING.WTFPL for more details.
//

#if defined HAVE_CONFIG_H
#   include "config.h"
#endif

#include "core.h"
#include "loldebug.h"

using namespace std;
using namespace lol;

#if USE_SDL && defined __APPLE__
#   include <SDL_main.h>
#endif

#if defined _WIN32
#   undef main /* FIXME: still needed? */
#   include <direct.h>
#endif

extern char const *lolfx_08_fbo;

class FBO : public WorldEntity
{
public:
    FBO()
    {
        m_vertices << vec2( 0.0,  0.8);
        m_vertices << vec2(-0.8, -0.8);
        m_vertices << vec2( 0.8, -0.8);
        m_ready = false;
    }

    virtual void TickDraw(float seconds)
    {
        WorldEntity::TickDraw(seconds);

        if (!m_ready)
        {
            m_shader = Shader::Create(lolfx_08_fbo);
            m_coord = m_shader->GetAttribLocation("in_Position", VertexUsage::Position, 0);

            m_vdecl = new VertexDeclaration(VertexStream<vec2>(VertexUsage::Position));

            m_vbo = new VertexBuffer(m_vertices.Bytes());
            void *vertices = m_vbo->Lock(0, 0);
            memcpy(vertices, &m_vertices[0], m_vertices.Bytes());
            m_vbo->Unlock();

            m_fbo = new FrameBuffer(Video::GetSize());

            m_ready = true;

            /* FIXME: this object never cleans up */
        }

        m_fbo->Bind();
        m_shader->Bind();
        m_vdecl->SetStream(m_vbo, m_coord);
        m_vdecl->Bind();
        m_vdecl->DrawElements(MeshPrimitive::Triangles, 0, 1);
        m_vdecl->Unbind();
        m_shader->Unbind();
        m_fbo->Unbind();

        m_shader->Bind();
        m_vdecl->SetStream(m_vbo, m_coord);
        m_vdecl->Bind();
        m_vdecl->DrawElements(MeshPrimitive::Triangles, 0, 1);
        m_vdecl->Unbind();
        m_shader->Unbind();
    }

private:
    Array<vec2> m_vertices;
    Shader *m_shader;
    ShaderAttrib m_coord;
    VertexDeclaration *m_vdecl;
    VertexBuffer *m_vbo;
    FrameBuffer *m_fbo;
    bool m_ready;
};

int main(int argc, char **argv)
{
    Application app("Tutorial 08: Framebuffer Object", ivec2(640, 480), 60.0f);

#if defined _MSC_VER && !defined _XBOX
    _chdir("..");
#elif defined _WIN32 && !defined _XBOX
    _chdir("../..");
#endif

    new DebugFps(5, 5);
    new FBO();

    app.Run();
    return EXIT_SUCCESS;
}

