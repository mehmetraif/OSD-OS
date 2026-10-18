-- OSD/OS's logo in a corner of the picture, the way a channel's sits in a
-- corner of a broadcast: the slash in its three colours, and OSD over OS. The
-- artwork is assets/images/logo-bug.svg, redrawn here as ASS vector shapes,
-- so it goes through mpv's OSD like a subtitle: in the picture, in both of the
-- app's modes, and under whatever the app draws over the picture.
-- MpvController loads it for every session while Settings' CHANNEL LOGO is
-- on, with the corner in script-opts (logo-corner=tl|tr|bl|br, or all for
-- one in each of the four). With Settings' LOGO IMAGE, a picture of the
-- user's own stands there instead: MpvController reads it into mpv's raw
-- overlay format at the size it is shown (logo-image, logo-width and
-- logo-height), and it is laid over the picture with overlay-add. It stands 7%
-- of the output's height tall and 10% of the picture's width and height in
-- from the picture's corner (the letterbox bars are outside it), inside a
-- CRT's safe area, and follows the output's size. On a tube's pixels, which
-- aren't square, it keeps its shape.
local assdraw = require 'mp.assdraw'
local options = require 'mp.options'

local opts = { corner = "tr", image = "", width = 0, height = 0 }
options.read_options(opts, "logo")

-- The artwork's frame, in its own units.
local ART = { x = 6, y = 31.856, w = 275.82, h = 129.114 }

-- Each shape: its colour as ASS writes one (BBGGRR) and its contours, each a
-- run of points; a hole runs the other way round, as nonzero filling needs.
local SHAPES = {
    { colour = "3D3DFF",  -- #ff3d3d
      contours = {
            { {59.75, 31.86}, {78.42, 31.86}, {24.67, 160.97}, {6, 160.97} },
      } },
    { colour = "FF6B2F",  -- #2f6bff
      contours = {
            { {78.42, 31.86}, {97.08, 31.86}, {43.33, 160.97}, {24.67, 160.97} },
      } },
    { colour = "63E02F",  -- #2fe063
      contours = {
            { {97.08, 31.86}, {115.75, 31.86}, {62, 160.97}, {43.33, 160.97} },
      } },
    { colour = "FFEEE8",  -- #e8eeff
      contours = {
            { {141.112, 88.968}, {141.112, 81.488}, {133.368, 81.488}, {133.368, 39.424}, {141.112, 39.424}, {141.112, 31.856}, {167.864, 31.856}, {167.864, 39.424}, {175.696, 39.424}, {175.696, 81.488}, {167.864, 81.488}, {167.864, 88.968} },
            { {144.544, 77}, {164.432, 77}, {164.432, 43.912}, {144.544, 43.912} },
            { {194.176, 88.968}, {194.176, 81.488}, {186.432, 81.488}, {186.432, 69.432}, {197.608, 69.432}, {197.608, 77}, {217.496, 77}, {217.496, 66.352}, {194.176, 66.352}, {194.176, 58.872}, {186.432, 58.872}, {186.432, 39.424}, {194.176, 39.424}, {194.176, 31.856}, {220.928, 31.856}, {220.928, 39.424}, {228.76, 39.424}, {228.76, 51.392}, {217.496, 51.392}, {217.496, 43.912}, {197.608, 43.912}, {197.608, 54.472}, {220.928, 54.472}, {220.928, 61.864}, {228.76, 61.864}, {228.76, 81.488}, {220.928, 81.488}, {220.928, 88.968} },
            { {239.496, 88.968}, {239.496, 31.856}, {266.248, 31.856}, {266.248, 39.424}, {273.992, 39.424}, {273.992, 46.904}, {281.824, 46.904}, {281.824, 73.92}, {273.992, 73.92}, {273.992, 81.488}, {266.248, 81.488}, {266.248, 88.968} },
            { {250.672, 77}, {262.816, 77}, {262.816, 69.432}, {270.56, 69.432}, {270.56, 51.392}, {262.816, 51.392}, {262.816, 43.912}, {250.672, 43.912} },
            { {141.112, 160.968}, {141.112, 153.488}, {133.368, 153.488}, {133.368, 111.424}, {141.112, 111.424}, {141.112, 103.856}, {167.864, 103.856}, {167.864, 111.424}, {175.696, 111.424}, {175.696, 153.488}, {167.864, 153.488}, {167.864, 160.968} },
            { {144.544, 149}, {164.432, 149}, {164.432, 115.912}, {144.544, 115.912} },
            { {194.176, 160.968}, {194.176, 153.488}, {186.432, 153.488}, {186.432, 141.432}, {197.608, 141.432}, {197.608, 149}, {217.496, 149}, {217.496, 138.352}, {194.176, 138.352}, {194.176, 130.872}, {186.432, 130.872}, {186.432, 111.424}, {194.176, 111.424}, {194.176, 103.856}, {220.928, 103.856}, {220.928, 111.424}, {228.76, 111.424}, {228.76, 123.392}, {217.496, 123.392}, {217.496, 115.912}, {197.608, 115.912}, {197.608, 126.472}, {220.928, 126.472}, {220.928, 133.864}, {228.76, 133.864}, {228.76, 153.488}, {220.928, 153.488}, {220.928, 160.968} },
      } },
}

local function draw()
    local dims = mp.get_property_native("osd-dimensions")
    if not dims or not dims.w or dims.w <= 0 or dims.h <= 0 then
        return
    end
    local W, H = dims.w, dims.h
    -- The picture's own rectangle within the output (a cropped picture runs
    -- past the edges: then the edges).
    local vx0 = math.max(0, dims.ml or 0)
    local vy0 = math.max(0, dims.mt or 0)
    local vx1 = math.min(W, W - (dims.mr or 0))
    local vy1 = math.min(H, H - (dims.mb or 0))
    local vw, vh = vx1 - vx0, vy1 - vy0
    if vw <= 0 or vh <= 0 then
        return
    end

    local mx, my = vw * 0.10, vh * 0.10
    local corners = opts.corner == "all" and { "tl", "tr", "bl", "br" } or { opts.corner }

    -- A picture of the user's own: an overlay in each corner, at its size.
    if opts.image ~= "" and opts.width > 0 and opts.height > 0 then
        for i, corner in ipairs(corners) do
            local left = corner == "tl" or corner == "bl"
            local top = corner == "tl" or corner == "tr"
            local x = left and (vx0 + mx) or (vx1 - mx - opts.width)
            local y = top and (vy0 + my) or (vy1 - my - opts.height)
            mp.command_native({ name = "overlay-add", id = i - 1,
                                x = math.max(0, math.floor(x)), y = math.max(0, math.floor(y)),
                                file = opts.image, offset = 0, fmt = "bgra",
                                w = opts.width, h = opts.height, stride = opts.width * 4 })
        end
        return
    end

    -- A tube's pixels aren't square (720×240 shown 4:3): mpv gives the
    -- output's pixel aspect (osd-dimensions' par, how many of its pixels
    -- across make one down) and shapes text for it, not drawings, so the
    -- drawing is made that much wider.
    local par = (dims.par and dims.par > 0) and dims.par or 1
    local h = H * 0.07
    local s = h / ART.h
    local sx = s * par
    local w = ART.w * sx

    local ass = assdraw.ass_new()
    for _, corner in ipairs(corners) do
        local left = corner == "tl" or corner == "bl"
        local top = corner == "tl" or corner == "tr"
        local x0 = left and (vx0 + mx) or (vx1 - mx - w)
        local y0 = top and (vy0 + my) or (vy1 - my - h)
        for _, shape in ipairs(SHAPES) do
            ass:new_event()
            ass:append(string.format("{\\an7\\pos(0,0)\\bord0\\shad0\\blur0\\1c&H%s&\\1a&H00&}", shape.colour))
            ass:draw_start()
            for _, contour in ipairs(shape.contours) do
                for i, p in ipairs(contour) do
                    local x = x0 + (p[1] - ART.x) * sx
                    local y = y0 + (p[2] - ART.y) * s
                    if i == 1 then
                        ass:move_to(x, y)
                    else
                        ass:line_to(x, y)
                    end
                end
            end
            ass:draw_stop()
        end
    end
    mp.set_osd_ass(W, H, ass.text)
end

-- Drawn as the output takes shape, and again whenever its size changes.
mp.observe_property("osd-dimensions", "native", draw)
