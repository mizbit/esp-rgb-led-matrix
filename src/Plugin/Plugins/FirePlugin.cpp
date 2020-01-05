/* MIT License
 *
 * Copyright (c) 2019 Andreas Merkle <web@blue-andi.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*******************************************************************************
    DESCRIPTION
*******************************************************************************/
/**
 * @brief  Fire demo plugin
 * @author Andreas Merkle <web@blue-andi.de>
 */

/******************************************************************************
 * Includes
 *****************************************************************************/
#include "FirePlugin.h"

/******************************************************************************
 * Compiler Switches
 *****************************************************************************/

/******************************************************************************
 * Macros
 *****************************************************************************/

/******************************************************************************
 * Types and classes
 *****************************************************************************/

/******************************************************************************
 * Prototypes
 *****************************************************************************/

/******************************************************************************
 * Local Variables
 *****************************************************************************/

/******************************************************************************
 * Public Methods
 *****************************************************************************/

void FirePlugin::active(IGfx& gfx)
{
    /* Defered constructor */
    if (NULL == m_heat)
    {
        m_heatSize = gfx.width() * gfx.height();
        m_heat = new uint8_t[m_heatSize];

        if (NULL == m_heat)
        {
            m_heatSize = 0u;
        }
    }
    
    /* Clear display */
    gfx.fillScreen(ColorDef::convert888To565(ColorDef::BLACK));

    return;
}

void FirePlugin::inactive(void)
{
    /* Nothing to do. */
    return;
}

void FirePlugin::update(IGfx& gfx)
{
    int16_t x       = 0;
    int16_t y       = 0;

    if (NULL == m_heat)
    {
        return;
    }

    for(x = 0; x < gfx.width(); ++x)
    {
        /* Step 1) Cool down every cell a little bit */
        for(y = 0; y < gfx.height(); ++y)
        {
            uint8_t     coolDownTemperature = random(0, ((COOLING * 10u) / gfx.height()) + 2u);
            uint32_t    heatPos             = x + y * gfx.width();

            if (coolDownTemperature >= m_heat[heatPos])
            {
                m_heat[heatPos] = 0u;
            }
            else
            {
                m_heat[heatPos] -= coolDownTemperature;
            }
        }

        /* Step 2) Heat from each cell drifts 'up' and diffuses a little bit */
        for(y = 0; y < (gfx.height() - 1); ++y)
        {
            uint16_t    diffusHeat  = 0u;
            
            if ((gfx.height() - 2) > y)
            {
                diffusHeat += m_heat[x + (y + 1) * gfx.width()];
                diffusHeat += m_heat[x + (y + 1) * gfx.width()];
                diffusHeat += m_heat[x + (y + 2) * gfx.width()];
                diffusHeat /= 3u;
            }
            else
            {
                diffusHeat += m_heat[x + (y + 0) * gfx.width()];
                diffusHeat += m_heat[x + (y + 0) * gfx.width()];
                diffusHeat += m_heat[x + (y + 1) * gfx.width()];
                diffusHeat /= 3u;
            }

            m_heat[x + y * gfx.width()] = diffusHeat;
        }

        /* Step 3) Randomly ignite new 'sparks' of heat near the bottom */
        if (random(0, 255) < SPARKING)
        {
            uint8_t     randValue   = random(160, 255);
            uint32_t    heatPos     = x + (gfx.height() - 1) * gfx.width();
            uint16_t    heat        = m_heat[heatPos] + randValue;

            if (UINT8_MAX < heat)
            {
                m_heat[heatPos] = 255u;
            }
            else
            {
                m_heat[heatPos] = heat;
            }
        }

        /* Step 4) Map from heat cells to LED colors */
        for(y = 0; y < gfx.height(); ++y)
        {
            gfx.drawPixel(x, y, heatColor(m_heat[x + y * gfx.width()]).to565());
        }
    }

    return;
}

/******************************************************************************
 * Protected Methods
 *****************************************************************************/

/******************************************************************************
 * Private Methods
 *****************************************************************************/

Color FirePlugin::heatColor(uint8_t temperature)
{
    Color heatColor;

    /* Scale 'heat' down from 0-255 to 0-191, which can then be easily divided
     * into three equal 'thirds' of 64 units each.
     */
    uint8_t t192        = static_cast<uint32_t>(temperature) * 191u / 255u;

    /* Calculate a value that ramps up from zero to 255 in each 'third' of the scale. */
    uint8_t heatRamp    = t192 & 0x3F; /* 0..63 */

    /* Scale up to 0..252 */
    heatRamp <<= 2;

    /* Now figure out which third of the spectrum we're in. */
    if (t192 & 0x80)
    {
        /* We're in the hottest third */
        heatColor.setRed(255u);         /* Full red */
        heatColor.setGreen(255u);       /* Full green */
        heatColor.setBlue(heatRamp);    /* Ramp up blue */
    }
    else if (t192 & 0x40)
    {
        /* We're in the middle third */
        heatColor.setRed(255u);         /* Full red */
        heatColor.setGreen(heatRamp);   /* Ramp up green */
        heatColor.setBlue(0u);          /* No blue */
    }
    else
    {
        /* We're in the coolest third */
        heatColor.setRed(heatRamp); /* Ramp up red */
        heatColor.setGreen(0u);     /* No green */
        heatColor.setBlue(0u);      /* No blue */
    }

    return heatColor;
}

/******************************************************************************
 * External Functions
 *****************************************************************************/

/******************************************************************************
 * Local Functions
 *****************************************************************************/