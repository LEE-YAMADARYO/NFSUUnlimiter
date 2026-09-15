# NFSU Unlimiter Fork Beta 1
NFSU Unlimiter is a script mod which fixes some issues for added cars.<br>
This fork is highly experimental and may contain bugs or cause the game to crash, please back up your profile/save file before installing.<br>

# New Features
- Fixed hardcoded issues in the car select screen for MainMenu Quick Race and Customization, now they can all display addon cars.<br>
- Fixed a crash that occurred when the number of cars manufacturer icons exceeded the available FNG slots.<br>
- Fixed invalid frontend cross-references, unlock flags that addon cars can never pass are cleared, and unique preset catalog keys stop addon car records from shadowing vanilla cars records.<br>

# Known Issues
In slots exceeding the FE limit, the manufacturer icon in the thumbnail slot repeats the icon of the previously placed vehicle, and the highlight does not move with the selection; This needs to be fixed by editing the FNG, see TODO for details.<br>

# TODO
- Add UnlimiterData configurations for each car, so the modders can configure if the cars can be used by AI opponents.<br>
- Edit frontend FNG files so every listed car gets its own thumbnail slot and manufacturer icon.<br>
- Extensive testing, including stability checks for Underground mode and profiles.<br>
etc.

# Download
- Compiled .asi files are available on the Releases page.<br>
- Ensure Ultimate-ASI-Loader is installed, then place the .asi and .ini files into the Scripts folder within the game directory.<br>
- If you want to compile it yourself, you can download the source code from the green Clone or Download button up there.
