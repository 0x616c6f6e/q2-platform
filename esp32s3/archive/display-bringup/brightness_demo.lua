-- Platform bring-up fixture: UI/event logic stays in Lua, hardware stays in Platform.
local q = require("q2")
local count, counter, red, green, blue = 0
local dimmer, brighter, brightness_label, brightness
local function show_brightness(failed)
    local text = brightness and ("Brightness: " .. brightness .. "%") or "Brightness unavailable"
    if failed then text = text .. " / ERROR" end
    assert(q.ui.set_text(brightness_label, text))
end
return {
    on_start = function()
        local w, h = assert(q.ui.width()), assert(q.ui.height())
        assert(q.ui.background(0x101c19))
        assert(q.ui.label(24, 20, "Q2 / ESP32-S3"))
        assert(q.ui.label(24, 50, "AM1.78 / CO5300"))
        assert(q.ui.label(24, 80, tostring(w) .. " x " .. tostring(h) .. " / QSPI"))
        counter = assert(q.ui.label(24, 110, "Touches: 0"))
        brightness_label = assert(q.ui.label(24, h - 204, "Brightness unavailable"))
        -- PC fixture has no physical panel provider; do not pretend it controls real brightness.
        if q.panel and q.panel.brightness then brightness = q.panel.brightness() end
        show_brightness(false)
        dimmer = assert(q.ui.button(24, h - 174, "- 10%", 144))
        brighter = assert(q.ui.button(w - 168, h - 174, "+ 10%", 144))
        red = assert(q.ui.button(24, h - 108, "RED", 92))
        green = assert(q.ui.button(128, h - 108, "GREEN", 92))
        blue = assert(q.ui.button(232, h - 108, "BLUE", 92))
    end,
    on_event = function(e)
        if e.type ~= "ui.click" then return end
        if e.value == dimmer or e.value == brighter then
            if not brightness then return end
            -- Keep the test UI visible; zero is supported by the provider but not exposed here.
            local target = math.max(10, math.min(100, brightness + (e.value == dimmer and -10 or 10)))
            if target == brightness then return end
            local applied = q.panel.brightness(target)
            if applied then brightness = applied end
            show_brightness(not applied)
            return
        end
        local colors = {[red] = 0x800000, [green] = 0x005000, [blue] = 0x000080}
        if colors[e.value] then
            count = count + 1
            assert(q.ui.background(colors[e.value]))
            assert(q.ui.set_text(counter, "Touches: " .. tostring(count)))
        end
    end
}
