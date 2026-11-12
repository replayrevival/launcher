# Controller button pictures

Source: Kenney "Input Prompts" 1.5, CC0 (see License.txt; https://kenney.nl/assets/input-prompts). The original download is kept here as `kenney_input-prompts_1.5.zip` (SHA-256 in CREDITS.md). It is not part of release packages.

Only the pictures the launcher uses are unpacked, flat in this folder, unchanged from the zip's `Xbox Series/Default` and `PlayStation Series/Default`:

playstation5_button_create.png playstation5_button_options.png playstation_button_circle.png playstation_button_cross.png playstation_button_l3.png playstation_button_r3.png playstation_button_square.png playstation_button_triangle.png playstation_dpad_down.png playstation_dpad_left.png playstation_dpad_right.png playstation_dpad_up.png playstation_trigger_l1.png playstation_trigger_l2.png playstation_trigger_r1.png playstation_trigger_r2.png xbox_button_a.png xbox_button_b.png xbox_button_back.png xbox_button_color_a.png xbox_button_color_b.png xbox_button_color_x.png xbox_button_color_y.png xbox_button_start.png xbox_button_x.png xbox_button_y.png xbox_dpad_down.png xbox_dpad_left.png xbox_dpad_right.png xbox_dpad_up.png xbox_lb.png xbox_ls.png xbox_lt.png xbox_rb.png xbox_rs.png xbox_rt.png 

Which picture goes with which button and Button prompts mode is `glyphFile()` in `src/core/input_map.cpp`:
- **Xbox**: white Xbox Series buttons; **Original Xbox**: the same, with the coloured A/B/X/Y. The original pad's Black and White buttons are not in the Kenney set; `ours_xbox_black.png` and `ours_xbox_white.png` are our own simple round buttons (drawn by `scripts/make_glyphs.py`, original art, GPL-2.0-or-later like the rest of the launcher, not CC0).
- **PlayStation**: PlayStation Series buttons (Create/Options for Back/Start).
- **Auto**: follows the connected pad (Xbox or PlayStation pictures). Switch pads, other pads and the keyboard show text key caps.

The launcher loads the PNGs with its own small decoder (`src/platform/png_decode.*`), so no image library is needed. To use another picture, add the PNG here and name it in `glyphFile()`.
