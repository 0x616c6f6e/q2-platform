-- Full-screen panel diagnostic: no widgets or touch-dependent transitions.
local q = require("q2")
local colors = {
    {name = "RED", value = 0xdd0000},
    {name = "GREEN", value = 0x00dd00},
    {name = "BLUE", value = 0x0000dd}
}
local interval_ms = 2000
local index, deadline, transitions = 1, nil, 0
local function show()
    local color = colors[index]
    assert(q.ui.background(color.value))
    transitions = transitions + 1
    -- Logging is optional: a build with logging disabled must still cycle the display.
    q.os.log("info", "RGB requested: " .. color.name .. " step=" .. transitions)
end
return {
    on_start = function()
        show()
    end,
    on_tick = function(now_ms)
        if not deadline then deadline = now_ms + interval_ms end
        if now_ms < deadline then return end
        -- Keep each color visible for a full interval, even if a prior refresh was delayed.
        index = index % #colors + 1
        deadline = now_ms + interval_ms
        show()
    end
}
