# STM32H7S78-DK_24bpp TBS

Performance testing can be done using the GPIO pins designated with the following signals in CN10 connector on the underside of the board:

- VSYNC_FREQ  - CN14-D2 (PF1)
- RENDER_TIME - CN14-D4 (PF2)
- FRAME_RATE  - CN14-D7 (PF3)
- MCU_ACTIVE  - CN15-D8 (PF4)

## Debugging in STM32CubeIDE
Debugging the code in an IDE can be complex because of the Bootloader and Application structure of the TBS for STM32H7S78-DK. To step through the code of the TouchGFX application in STM32CubeIDE, follow these steps:
1. Generate code in TouchGFX Designer
2. Open the project in STM32CubeIDE
3. Launch a debug session for the Boot project
4. Wait for the compilation and flashing to complete
5. Terminate the debug session (Ctrl + F2)
6. Launch a debug session for the Appli project
7. Wait for the compilation and flashing to complete
8. Click Resume (F8)
9. Press the black NRST button on the STM32H7S7-DK board
10. The application is now at a break point at the first line of main() in the Appli project. If not, click Resume (F8) once more
11. Proceed by e.g. clicking Resume (F8) or Step Over (F6)

## Home Offset / ORG Reset Rules

The project uses two different concepts and they must not be mixed:

- Home Offset (Parameter Page): user parameter value used by position UI behavior.
- Internal Home Origin (home_hw): internal hardware origin used in position conversion.

### Intended behavior

1. `Write All`
	- Applies motion parameters (jog speed, acc/dec, limits, unit scale, gain).
	- Saves Parameter Page values to parameter flash.
	- Must not overwrite internal home origin from the Home Offset field.

2. `ORG Reset (Set Home)`
	- Uses current hardware position and configured Home Offset(user) to update internal home origin:
	  - `home_hw = actual_hw - (home_offset_user * unit_scale)`
	- Immediately after ORG Reset, Current Position should display Home Offset(user).
	- Persists internal home origin to home flash.

3. `Read All`
	- Refreshes drive-side motion parameters into Parameter Page.
	- Must not replace Home Offset field with internal home origin or raw encoder-like values.

4. Home diagnostics (optional)
	- Runtime UART command: `CMD,diag_home_log=1` (enable), `CMD,diag_home_log=0` (disable).
	- When enabled, firmware prints `[HOME_DIAG]` snapshots at key points:
	  - parameter apply/write-all
	  - ORG Reset (set_home)
	  - parameter read-all completion

### Quick verification sequence

1. Enter `Home Offset = 100` in Parameter Page.
2. Press `Write All`.
3. Press `Read All` and confirm Home Offset stays `100`.
4. Go to Home Mode and press `ORG Reset`.
5. Confirm Current Position becomes `100`.
6. Press `Read All` again and confirm Home Offset and Current Position do not jump to raw encoder-style values.