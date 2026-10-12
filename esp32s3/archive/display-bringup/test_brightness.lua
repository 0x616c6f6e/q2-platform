-- Run from q2 root with a host Lua interpreter. Inject hardware outcomes; execute the real fixture.
local source = "platform/esp32s3/main/brightness_demo.lua"
for _, size in ipairs({{368, 448}, {448, 368}}) do
    for _, supported in ipairs({true, false}) do
        local next_id, buttons, labels, writes = 0, {}, {}, {}
        local percent, fail, background = 30, false, 0
        local api = {ui = {}}
        api.ui.width = function() return size[1] end
        api.ui.height = function() return size[2] end
        api.ui.background = function(c) background = c return true end
        api.ui.label = function(x, y, text)
            assert(y >= 0 and y + 16 <= size[2])
            next_id = next_id + 1
            labels[next_id] = text
            return next_id
        end
        api.ui.button = function(x, y, text, width)
            assert(x >= 0 and x + width <= size[1] and y >= 0 and y + 48 <= size[2])
            next_id = next_id + 1
            buttons[text] = next_id
            return next_id
        end
        api.ui.set_text = function(id, text) assert(labels[id]) labels[id] = text return true end
        if supported then
            api.panel = {brightness = function(value)
                if value then
                    writes[#writes + 1] = value
                    if fail then return nil, -15 end
                    percent = value
                end
                return percent
            end}
        end
        local env = setmetatable({require = function(name) assert(name == "q2") return api end}, {__index = _G})
        local app = assert(loadfile(source, "t", env))()
        app.on_start()
        local function click(name) app.on_event({type = "ui.click", value = assert(buttons[name])}) end
        local function has_label(text)
            for _, value in pairs(labels) do if value == text then return true end end
            return false
        end
        click("- 10%")
        if supported then
            assert(percent == 20 and has_label("Brightness: 20%"))
            for _ = 1, 20 do click("- 10%") end
            assert(percent == 10 and #writes == 2)
            for _ = 1, 20 do click("+ 10%") end
            assert(percent == 100 and #writes == 11)
            fail = true
            click("- 10%")
            assert(percent == 100 and has_label("Brightness: 100% / ERROR"))
            fail = false
            click("- 10%")
            assert(percent == 90 and has_label("Brightness: 90%"))
        else
            assert(#writes == 0 and has_label("Brightness unavailable"))
        end
        click("GREEN")
        assert(background == 0x005000 and has_label("Touches: 1"))
        click("RED")
        assert(background == 0x800000 and has_label("Touches: 2"))
        print(string.format("PASS %dx%d brightness supported=%s", size[1], size[2], tostring(supported)))
    end
end
