﻿//
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

#if HAVE_CONFIG_H
#   include "config.h"
#endif
#if defined LOL_USE_XINPUT
#   include <d3d9.h>
#   include <xinput.h>
#endif

#include <lol/engine-internal.h>

#include "ui/d3d9-input.h"
#include "ui/input.h"

namespace lol
{

static const std::string g_name_xbox_axis_left_x("Axis1");
static const std::string g_name_xbox_axis_left_y("Axis2");
static const std::string g_name_xbox_axis_right_x("Axis3");
static const std::string g_name_xbox_axis_right_y("Axis4");
static const std::string g_name_xbox_axis_left_trigger("Axis5");
static const std::string g_name_xbox_axis_right_trigger("Axis6");

/*
 * D3d9 Input implementation class
 */

class D3d9InputData
{
    friend class D3d9Input;

private:
#if defined LOL_USE_XINPUT
    array<int, InputDevice*> m_joysticks;
#endif // LOL_USE_XINPUT
};

/*
 * Public D3d9Input class
 */

D3d9Input::D3d9Input()
  : m_data(new D3d9InputData())
{
#if defined LOL_USE_XINPUT
    for (int i = 0; i < XUSER_MAX_COUNT; i++)
    {
        XINPUT_STATE state;
        if (XInputGetState(i, &state) != ERROR_SUCCESS)
            continue;
        // TODO: we can put more friendly name here, such as LeftAxisX, ButtonX...
        InputDevice* stick = new InputDevice(g_name_joystick(i + 1));

        stick->AddAxis(g_name_xbox_axis_left_x.c_str());
        stick->AddAxis(g_name_xbox_axis_left_y.c_str());
        stick->AddAxis(g_name_xbox_axis_right_x.c_str());
        stick->AddAxis(g_name_xbox_axis_right_y.c_str());
        stick->AddAxis(g_name_xbox_axis_left_trigger.c_str());
        stick->AddAxis(g_name_xbox_axis_right_trigger.c_str());

        #define _BTN(id, name) stick->internal_add_button(input::button::BTN_##name, #name);
        #include "ui/buttons.inc" // FIXME: ignore mouse buttons here

        m_data->m_joysticks.push(i, stick);
    }
#endif

    m_gamegroup = tickable::group::game::input;
}

D3d9Input::~D3d9Input()
{
#if defined LOL_USE_XINPUT
    /* Unregister all the joysticks we added */
    while (m_data->m_joysticks.count())
    {
        delete m_data->m_joysticks[0].m2;
        m_data->m_joysticks.remove(0);
    }
#endif
    delete m_data;
}

void D3d9Input::tick_game(float seconds)
{
    Entity::tick_game(seconds);

#if defined LOL_USE_XINPUT
    for (int i = 0; i < m_data->m_joysticks.count(); i++)
    {
        XINPUT_STATE state;
        if (XInputGetState(m_data->m_joysticks[i].m1, &state) != ERROR_SUCCESS)
            continue;

        m_data->m_joysticks[i].m2->internal_set_axis(0, (float)state.Gamepad.sThumbLX / 32768.f);
        m_data->m_joysticks[i].m2->internal_set_axis(1, -(float)state.Gamepad.sThumbLY / 32768.f);
        m_data->m_joysticks[i].m2->internal_set_axis(2, (float)state.Gamepad.sThumbRX / 32768.f);
        m_data->m_joysticks[i].m2->internal_set_axis(3, -(float)state.Gamepad.sThumbRY / 32768.f);
        m_data->m_joysticks[i].m2->internal_set_axis(4, (float)state.Gamepad.bLeftTrigger / 32768.f);
        m_data->m_joysticks[i].m2->internal_set_axis(5, (float)state.Gamepad.bRightTrigger / 32768.f);

        for (int b = 0; b < 16; b++)
        {
            // Reserved values
            if ((1 << b) > XINPUT_GAMEPAD_RIGHT_SHOULDER && (1 << b) < XINPUT_GAMEPAD_A)
                continue;

            int key_index = (1 << b) > XINPUT_GAMEPAD_RIGHT_SHOULDER ? b - 2 : b;

            m_data->m_joysticks[i].m2->internal_set_button((input::button)key_index, ((uint16_t)(state.Gamepad.wButtons) >> b) & 1);
        }
    }
#endif
}

void D3d9Input::tick_draw(float seconds, Scene &scene)
{
    Entity::tick_draw(seconds, scene);
}

} /* namespace lol */

