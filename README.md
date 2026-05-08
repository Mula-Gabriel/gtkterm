
# GTKTerm Fork

1. Separate Macros File
- Macros and lists are stored in a standalone .ini file, independent from the serial port config (.gtktermrc) 
- Menu items "Load macros file..." and "Save macros file" open a file chooser dialog
- The last used macros file path is saved in .gtktermrc and auto-loaded at startup
                                                                                                              
2. Value Lists (New "Lists" Tab)
- In the Macros configuration window, a new "Lists" tab lets you define named lists of display/value pairs
- Each list entry has a List name, Display text (shown in UI), and Value (sent to serial port)
- Lists are referenced in macro actions using %#ListName syntax
- Example: A list called "Commands" with entries like "Reset" → "AT+RESET", "Ping" → "AT+PING"
                                                                                                              
3. Macros with Format Arguments
- Macro actions support printf-style format specifiers:
  - %s – string
  - %d / %i – signed integer
  - %u / %o / %x – unsigned integer (decimal/octal/hex)
  - %f – 32-bit float (~7 significant digits)
  - %lf – 64-bit double (~15 significant digits)
  - %Lf – 80/128-bit long double
  - %c – single character
- Macros with arguments automatically generate buttons with input fields in the macro panel
- Example action: "AT+SEND=%s\r\n" → user types the string, macro sends the full command
- A `[name]` tag placed immediately before a format specifier adds a label above its input field
- Example: "AT+SPEED=[Speed]%d\r\n" → the integer field is labeled "Speed" in the UI
- Works with list arguments too: "[Command]%#Commands" → the dropdown is labeled "Command"
                                                                                                              
4. List Arguments in Macros
- Using %#ListName in a macro action creates a combo box dropdown in the button UI
- User selects from the predefined list values before sending
- Can be combined with regular format args: "CMD=%#Commands=%d\r\n"
                                                                                                              
5. Reorder Buttons (Move Up/Down)
- Both the Macros tab and Lists tab have "Move Up" / "Move Down" buttons
- Lets you reorder macros and list entries to match your preferred workflow

6. TCP Client / Server Transport
- Supports three transport modes: Serial port, TCP Client, and TCP Server
- TCP Client connects to a remote host:port with optional auto-reconnect (2-second interval)
- TCP Server listens on a port and accepts a single client connection (new client replaces old)
- Privileged ports (<1024) are supported via Linux CAP_NET_BIND_SERVICE capability
- Switch transport type in Config > Port dialog via a combo box; GUI adapts to show relevant settings
- Command-line options: --transport (serial|tcp-client|tcp-server), --host, --tcp-port

7. Smart Add (Insert After Selection)
- In the Macros tab, clicking "Add" inserts a new macro right after the currently selected row
- In the Lists tab, clicking "Add Entry" inserts a new entry right after the currently selected row
- If no row is selected, the new item is appended at the end of the list as before

8. Macro Tab Groups
- Each macro has an optional **Tab** field in the configuration window
- Macros sharing the same tab name are grouped under a common tab in the macro panel
- Macros with no tab name are placed in the **General** tab automatically
- The tab bar wraps across multiple rows if there are many groups
- Right-clicking anywhere on the tab bar shows a checklist of all tabs, allowing you to show or hide each group independently

9. Macro Polling (Right-click on a macro button)
- Right-clicking any macro button opens a context menu with two options:
  - **Polling Mode** checkbox: enables or disables periodic auto-send for that macro
  - **Period (ms)** field: sets the repeat interval in milliseconds (default: 1000 ms)
- When polling is enabled, the button label gains a ⏱ prefix to indicate the mode
- **Left-clicking** the button then acts as a start/stop toggle:
  - First click: sends the macro immediately and starts repeating it at the configured interval
  - Second click: stops the periodic sending
- While actively polling, the button blinks to provide a visual indicator
- For macros with format arguments, the argument values entered at start time are captured and reused for every subsequent send
- The polling configuration (enabled state and period) is saved automatically to the macros file and restored at startup


<p align="center">
    <img src="Capture1.png" width="60%"/>
</p>
<p align="center">
    <img src="Capture2.png" width="60%"/>
</p>
<p align="center">
    <img src="Capture3.png" width="60%"/>
</p>
<p align="center">
    <img src="Capture4.png" width="60%"/>
    </p>
<p align="center">
    <img src="Capture5.png" width="60%"/>
    </p>
<p align="center">
    <img src="Capture6.png" width="60%"/>
</p>
# GTKTerm: A GTK+ Serial Port Terminal
<img src="data/gtkterm_256x256.png" align="right" width="20%"/>


GTKTerm is a simple, graphical serial port terminal emulator for Linux and possibly other POSIX-compliant operating systems. It can be used to communicate with all kinds of devices with a serial interface, such as embedded computers, microcontrollers, modems, GPS receivers, CNC machines and more.


## Usage
### Keyboard Shortcuts 
As GTKTerm is often used like a terminal emulator,
the shortcut keys are assigned to `<ctrl><shift>`, rather than just `<ctrl>`. This allows the user to send keystrokes of the form `<ctrl>X` and not have GTKTerm intercept them.

Key Combination | Effect
---:|---
`<ctrl><shift>L` | Clear screen
`<ctrl><shift>R` | Send file
`<ctrl><shift>Q` | Quit
`<ctrl><shift>S` | Configure port
`<ctrl><shift>V` | Paste
`<ctrl><shift>C` | Copy
`<ctrl><shift>F` | Find
`<ctrl><shift>K` | Clear Scrollback
`<ctrl><shift>A` | Select All
`<ctrl><shift>B` | Send Break
`<ctrl>B` | Send break
F5 | Open Port
F6 | Close Port
F7 | Toggle DTR
F8 | Toggle RTS

### Command Line Options
See `man gtkterm` or `gtkterm --help` for more information on available command line interface options.

### Notes on RS485:
The RS485 flow control is a software user-space emulation and therefore may not work for all configurations (won't respond quickly enough). If this is the case for your setup, you will need to either use a dedicated RS232 to RS485 converter, or look for a kernel level driver. This is an inherent limitation to user space programs.

### Scriptability with Signals
Some microcontrollers and other embedded devices are flashed using the same serial interface that is also used for outputting debug information. To facilitate rapid development on these platforms, GTKTerm supports the following UNIX signals:

Signal | Action | Usage Example
---:|:---:|---
`SIGUSR1` | Open Port | `killall -USR1 gtkterm`
`SIGUSR2` | Close Port | `killall -USR2 gtkterm`

You may find it useful to send these signals in your own firmware flashing scripts.

## Installation
GTKTerm has a few dependencies-
* Gtk+3.0 (version 3.12 or higher)
* vte (version 0.40 or higher)
* intltool (version 0.40.0 or higher)
* libgudev (version 229 or higher)

Once these dependencies are installed, most people should simply run:

	meson build
	ninja -C build

To install GTKTerm system-wide, run:

	ninja -C build install
	gtk-update-icon-cache

If you wish to install GTKTerm someplace other than the default directory, e.g. in `/usr`, use:

	meson build -Dprefix=/usr

Then build and install as usual.

## Uninstallation
To uninstall GTKTerm, run:

	ninja -C build uninstall

If you already deleted the `build` directory, just compile and install GTKTerm again as explained in the [previous section](#installation) with the same target location prefix (`-Dprefix`) and perform the uninstall step afterwards.

## License
Original Code by: Julien Schmitt

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
