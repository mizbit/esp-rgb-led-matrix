/* MIT License
 *
 * Copyright (c) 2019 - 2020 Andreas Merkle <web@blue-andi.de>
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
 * @brief  Display manager
 * @author Andreas Merkle <web@blue-andi.de>
 */

/******************************************************************************
 * Includes
 *****************************************************************************/
#include "DisplayMgr.h"
#include "LedMatrix.h"
#include "AmbientLightSensor.h"
#include "Settings.h"
#include "BrightnessCtrl.h"

#include <Logging.h>

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

/* Initialize LED matrix instance. */
DisplayMgr  DisplayMgr::m_instance;

/******************************************************************************
 * Public Methods
 *****************************************************************************/

bool DisplayMgr::begin()
{
    bool status = false;

    /* Set the display brightness here just once.
     * There is no need to do this in the process() method periodically.
     */
    BrightnessCtrl::getInstance().init();
    BrightnessCtrl::getInstance().setBrightness(BRIGHTNESS_DEFAULT);

    /* No slots available? */
    if (nullptr == m_slots)
    {
        if (false == Settings::getInstance().open(true))
        {
            m_maxSlots = Settings::getInstance().getMaxSlots().getDefault();

            LOG_WARNING("Using default number of max. slots.");
        }
        else
        {
            m_maxSlots = Settings::getInstance().getMaxSlots().getValue();
            Settings::getInstance().close();
        }

        if (0 < m_maxSlots)
        {
            m_slots = new Slot[m_maxSlots];
        }
    }

    /* Not started yet? */
    if ((nullptr == m_taskHandle) &&
        (nullptr != m_slots))
    {
        /* Create mutex to lock/unlock display update */
        m_xMutex = xSemaphoreCreateRecursiveMutex();

        /* Create binary semaphore to signal task exit. */
        m_xSemaphore = xSemaphoreCreateBinary();

        if ((nullptr != m_xMutex) &&
            (nullptr != m_xSemaphore))
        {
            BaseType_t  osRet   = pdFAIL;

            /* Task shall run */
            m_taskExit = false;

            osRet = xTaskCreateUniversal(   updateTask,
                                            "displayTask",
                                            TASK_STACKE_SIZE,
                                            this,
                                            4,
                                            &m_taskHandle,
                                            TASK_RUN_CORE);

            /* Task successful created? */
            if (pdPASS == osRet)
            {
                (void)xSemaphoreGive(m_xSemaphore);
                status = true;
            }
        }
    }

    /* Any error happened? */
    if (false == status)
    {
        if (nullptr != m_xMutex)
        {
            vSemaphoreDelete(m_xMutex);
            m_xMutex = nullptr;
        }

        if (nullptr != m_xSemaphore)
        {
            vSemaphoreDelete(m_xSemaphore);
            m_xSemaphore = nullptr;
        }
    }
    else
    {
        LOG_INFO("DisplayMgr is up.");
    }

    return status;
}

void DisplayMgr::end()
{
    /* Note, don't destroy the slots here. They shall live until the system restarts. */

    /* Already running? */
    if (nullptr != m_taskHandle)
    {
        m_taskExit = true;

        /* Join */
        (void)xSemaphoreTake(m_xSemaphore, portMAX_DELAY);
        m_taskHandle = nullptr;

        LOG_INFO("DisplayMgr is down.");

        vSemaphoreDelete(m_xSemaphore);
        m_xSemaphore = nullptr;

        vSemaphoreDelete(m_xMutex);
        m_xMutex = nullptr;
    }

    return;
}

bool DisplayMgr::setAutoBrightnessAdjustment(bool enable)
{
    bool status = false;

    lock();
    status = BrightnessCtrl::getInstance().enable(enable);
    unlock();

    return status;
}

bool DisplayMgr::getAutoBrightnessAdjustment(void)
{
    bool isEnabled = false;

    lock();
    isEnabled = BrightnessCtrl::getInstance().isEnabled();
    unlock();

    return isEnabled;
}

void DisplayMgr::setBrightness(uint8_t level)
{
    lock();
    BrightnessCtrl::getInstance().setBrightness(level);
    unlock();

    return;
}

uint8_t DisplayMgr::getBrightness(void)
{
    uint8_t brightness = 0U;

    lock(),
    brightness = BrightnessCtrl::getInstance().getBrightness();
    unlock();

    return brightness;
}

uint8_t DisplayMgr::installPlugin(IPluginMaintenance* plugin, uint8_t slotId)
{
    if (nullptr == plugin)
    {
        slotId = SLOT_ID_INVALID;
    }
    else
    {
        /* Install to any available slot? */
        if (SLOT_ID_INVALID == slotId)
        {
            lock();

            /* Find a empty unlocked slot. */
            slotId = 0U;
            while((m_maxSlots > slotId) && ((false == m_slots[slotId].isEmpty()) || (true == m_slots[slotId].isLocked())))
            {
                ++slotId;
            }

            if (m_maxSlots > slotId)
            {
                if (false == m_slots[slotId].setPlugin(plugin))
                {
                    slotId = SLOT_ID_INVALID;
                }
                else
                {
                    plugin->start();
                }
            }
            else
            {
                slotId = SLOT_ID_INVALID;
            }

            unlock();
        }
        /* Install to specific slot? */
        else if ((m_maxSlots > slotId) &&
                 (true == m_slots[slotId].isEmpty()) &&
                 (false == m_slots[slotId].isLocked()))
        {
            lock();

            if (false == m_slots[slotId].setPlugin(plugin))
            {
                slotId = SLOT_ID_INVALID;
            }
            else
            {
                plugin->start();
            }

            unlock();
        }
        else
        {
            slotId = SLOT_ID_INVALID;
        }

        if (m_maxSlots > slotId)
        {
            LOG_INFO("Plugin %s installed in slot %u.", plugin->getName(), slotId);
        }
    }

    return slotId;
}

bool DisplayMgr::uninstallPlugin(IPluginMaintenance* plugin)
{
    bool status = false;

    if (nullptr != plugin)
    {
        uint8_t slotId = SLOT_ID_INVALID;

        lock();

        slotId = getSlotIdByPluginUID(plugin->getUID());

        if (m_maxSlots > slotId)
        {
            if (false == m_slots[slotId].isLocked())
            {
                /* Is this plugin selected at the moment? */
                if (m_selectedPlugin == plugin)
                {
                    /* Remove selection */
                    m_selectedPlugin = nullptr;
                }

                plugin->stop();
                if (false == m_slots[slotId].setPlugin(nullptr))
                {
                    LOG_FATAL("Internal error.");
                }
                else
                {
                    status = true;
                }
            }
        }

        unlock();

        if (false == status)
        {
            LOG_INFO("Couldn't remove plugin %s (uid %u) from slot %u, because slot is locked.", plugin->getName(), plugin->getUID(), slotId);
        }
        else
        {
            LOG_INFO("Plugin %s (uid %u) removed from slot %u.", plugin->getName(), plugin->getUID(), slotId);
        }
    }

    return status;
}

uint8_t DisplayMgr::getSlotIdByPluginUID(uint16_t uid)
{
    uint8_t index   = 0U;
    uint8_t slotId  = SLOT_ID_INVALID;

    lock();

    while((m_maxSlots > index) && (m_maxSlots <= slotId))
    {
        if (false == m_slots[index].isEmpty())
        {
            if (uid == m_slots[index].getPlugin()->getUID())
            {
                slotId = index;
            }
        }

        ++index;
    }

    unlock();

    return slotId;
}

IPluginMaintenance* DisplayMgr::getPluginInSlot(uint8_t slotId)
{
    IPluginMaintenance* plugin = nullptr;

    if (m_maxSlots > slotId)
    {
        lock();

        plugin = m_slots[slotId].getPlugin();

        unlock();
    }

    return plugin;
}

void DisplayMgr::activatePlugin(IPluginMaintenance* plugin)
{
    if (nullptr != plugin)
    {
        uint8_t slotId = SLOT_ID_INVALID;

        lock();

        slotId = getSlotIdByPluginUID(plugin->getUID());

        if (m_maxSlots > slotId)
        {
            m_requestedPlugin = plugin;
        }

        unlock();
    }

    return;
}

bool DisplayMgr::movePluginToSlot(IPluginMaintenance* plugin, uint8_t slotId)
{
    bool status = false;

    if ((nullptr != plugin) &&
        (m_maxSlots > slotId))
    {
        uint8_t srcSlotId = getSlotIdByPluginUID(plugin->getUID());

        if ((m_maxSlots > srcSlotId) &&
            (srcSlotId != slotId))
        {
            Slot*   srcSlot = &m_slots[srcSlotId];
            Slot*   dstSlot = &m_slots[slotId];

            lock();

            if (false == dstSlot->isLocked())
            {
                srcSlot->setPlugin(dstSlot->getPlugin());
                dstSlot->setPlugin(plugin);

                /* Is one of the moved plugins selected at the moment? */
                if ((m_selectedPlugin == srcSlot->getPlugin()) ||
                    (m_selectedPlugin == dstSlot->getPlugin()))
                {
                    /* Remove selection */
                    m_selectedPlugin = nullptr;
                }

                status = true;
            }

            unlock();
        }
    }

    return status;
}

void DisplayMgr::lockSlot(uint8_t slotId)
{
    if (m_maxSlots > slotId)
    {
        lock();

        m_slots[slotId].lock();

        unlock();
    }

    return;
}

void DisplayMgr::unlockSlot(uint8_t slotId)
{
    if (m_maxSlots > slotId)
    {
        lock();

        m_slots[slotId].unlock();

        unlock();
    }

    return;
}

bool DisplayMgr::isSlotLocked(uint8_t slotId)
{
    bool isLocked = true;

    if (m_maxSlots > slotId)
    {
        lock();

        isLocked = m_slots[slotId].isLocked();

        unlock();
    }

    return isLocked;
}

uint32_t DisplayMgr::getSlotDuration(uint8_t slotId)
{
    uint32_t duration = 0U;

    if (m_maxSlots > slotId)
    {
        lock();

        duration = m_slots[slotId].getDuration();

        unlock();
    }

    return duration;
}

bool DisplayMgr::setSlotDuration(uint8_t slotId, uint32_t duration)
{
    bool status = false;

    if (m_maxSlots > slotId)
    {
        lock();

        m_slots[slotId].setDuration(duration);

        unlock();
    }

    return status;
}

void DisplayMgr::getFBCopy(uint32_t* fb, size_t length, uint8_t* slotId)
{
    if ((nullptr != fb) &&
        (0 < length))
    {
        LedMatrix&  matrix  = LedMatrix::getInstance();
        int16_t     x       = 0;
        int16_t     y       = 0;
        size_t      index   = 0;

        lock();

        /* Copy framebuffer after it is completely updated. */
        for(y = 0; y < matrix.height(); ++y)
        {
            for(x = 0; x < matrix.width(); ++x)
            {
                fb[index] = ColorDef::convert565To888(matrix.getColor(x, y));
                ++index;

                if (length <= index)
                {
                    break;
                }
            }
        }

        if (NULL != slotId)
        {
            *slotId = m_selectedSlot;
        }

        unlock();
    }

    return;
}

/******************************************************************************
 * Protected Methods
 *****************************************************************************/

/******************************************************************************
 * Private Methods
 *****************************************************************************/

DisplayMgr::DisplayMgr() :
    m_xMutex(nullptr),
    m_taskHandle(nullptr),
    m_taskExit(false),
    m_xSemaphore(nullptr),
    m_slots(nullptr),
    m_maxSlots(0U),
    m_selectedSlot(SLOT_ID_INVALID),
    m_selectedPlugin(nullptr),
    m_requestedPlugin(nullptr),
    m_slotTimer()
{
}

DisplayMgr::~DisplayMgr()
{
    /* Will never be called. */
}

uint8_t DisplayMgr::nextSlot(uint8_t slotId)
{
    uint8_t count = 0U;

    if (m_maxSlots <= slotId)
    {
        slotId = 0U;
    }
    else
    {
        ++slotId;
        slotId %= m_maxSlots;
    }

    /* Set next slot active, precondition is a installed plugin which is enabled.  */
    do
    {
        /* Plugin installed? */
        if (false == m_slots[slotId].isEmpty())
        {
            /* Plugin enabled? */
            if (true == m_slots[slotId].getPlugin()->isEnabled())
            {
                break;
            }
        }

        ++slotId;
        slotId %= m_maxSlots;

        ++count;
    }
    while (m_maxSlots > count);

    if (m_maxSlots <= count)
    {
        slotId = SLOT_ID_INVALID;
    }

    return slotId;
}

void DisplayMgr::process()
{
    LedMatrix&  matrix  = LedMatrix::getInstance();
    uint8_t     index   = 0U;

    lock();

    /* Handle display brightness */
    BrightnessCtrl::getInstance().process();

    /* Plugin requested to choose? */
    if (nullptr != m_requestedPlugin)
    {
        /* Plugin must be enabled. */
        if (false == m_requestedPlugin->isEnabled())
        {
            LOG_WARNING("Requested plugin %s (uid %u) in slot %u is disabled.",
                m_requestedPlugin->getName(),
                m_requestedPlugin->getUID(),
                getSlotIdByPluginUID(m_requestedPlugin->getUID()));
            m_requestedPlugin = nullptr;
        }
        /* Plugin selected? */
        else if (nullptr != m_selectedPlugin)
        {
            /* Remove selected plugin, which forces to select the requested one. */
            m_selectedPlugin->inactive();
            m_selectedPlugin = nullptr;
        }
        else
        {
            /* Nothing to do. */
            ;
        }
    }

    /* Plugin selected? */
    if (nullptr != m_selectedPlugin)
    {
        m_selectedSlot = getSlotIdByPluginUID(m_selectedPlugin->getUID());

        /* Plugin disabled in the meantime? */
        if (false == m_selectedPlugin->isEnabled())
        {
            m_selectedPlugin->inactive();
            m_selectedPlugin = nullptr;
            m_slotTimer.stop();
        }
        /* Plugin run duration timeout? */
        else if ((true == m_slotTimer.isTimerRunning()) &&
                 (true == m_slotTimer.isTimeout()))
        {
            uint8_t slotId = nextSlot(m_selectedSlot);

            /* If the next slot is the same as the current slot,
             * just restart the plugin duration timer.
             */
            if (m_selectedSlot == slotId)
            {
                if (true == m_slotTimer.isTimerRunning())
                {
                    m_slotTimer.restart();
                }
            }
            else
            {
                m_selectedPlugin->inactive();
                m_selectedPlugin = nullptr;
                m_slotTimer.stop();
            }
        }
        else
        {
            /* Nothing to do. */
            ;
        }
    }

    /* No plugin selected? */
    if (nullptr == m_selectedPlugin)
    {
        /* Plugin requested to choose? */
        if (nullptr != m_requestedPlugin)
        {
            m_selectedSlot      = getSlotIdByPluginUID(m_requestedPlugin->getUID());
            m_requestedPlugin   = nullptr;
        }
        /* Select next slot, which contains a enabled plugin. */
        else
        {
            m_selectedSlot = nextSlot(m_selectedSlot);
        }

        /* Next enabled plugin found? */
        if (m_maxSlots > m_selectedSlot)
        {
            uint32_t duration = 0U;

            m_selectedPlugin    = m_slots[m_selectedSlot].getPlugin();
            duration            = m_slots[m_selectedSlot].getDuration();

            if (0U != duration)
            {
                m_slotTimer.start(duration);
            }

            m_selectedPlugin->active(matrix);
            LOG_INFO("Slot %u (%s) now active.", m_selectedSlot, m_selectedPlugin->getName());
        }
    }

    /* Process all installed plugins. */
    for(index = 0U; index < m_maxSlots; ++index)
    {
        IPluginMaintenance* plugin = m_slots[index].getPlugin();

        if (nullptr != plugin)
        {
            plugin->process();
        }
    }

    /* Update display */
    if (nullptr != m_selectedPlugin)
    {
        m_selectedPlugin->update(matrix);
    }

    matrix.show();

    unlock();

    return;
}

void DisplayMgr::updateTask(void* parameters)
{
    DisplayMgr* displayMgr = reinterpret_cast<DisplayMgr*>(parameters);

    if ((nullptr != displayMgr) &&
        (nullptr != displayMgr->m_xSemaphore))
    {
        (void)xSemaphoreTake(displayMgr->m_xSemaphore, portMAX_DELAY);

        while(false == displayMgr->m_taskExit)
        {
            /* Refresh display content periodically */
            displayMgr->process();

            /* Wait until the physical update is ready to avoid flickering
             * and artifacts on the display, because of e.g. webserver flash
             * access.
             */
            while(false == LedMatrix::getInstance().isReady())
            {
                ;
            }

            delay(TASK_PERIOD);
        }

        (void)xSemaphoreGive(displayMgr->m_xSemaphore);
    }

    vTaskDelete(nullptr);

    return;
}

void DisplayMgr::lock()
{
    if (nullptr != m_xMutex)
    {
        (void)xSemaphoreTakeRecursive(m_xMutex, portMAX_DELAY);
    }

    return;
}

void DisplayMgr::unlock()
{
    if (nullptr != m_xMutex)
    {
        (void)xSemaphoreGiveRecursive(m_xMutex);
    }

    return;
}

/******************************************************************************
 * External Functions
 *****************************************************************************/

/******************************************************************************
 * Local Functions
 *****************************************************************************/
