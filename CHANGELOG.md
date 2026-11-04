# AP Home Start - Winter Cove - changelog

## 2.0.0 - 2026-11-04 - untested

### Changed
- The plugin now has Winter Cove.esp as a master, so it cannot load without Winter Cove and must load below it. Without Winter Cove the game stops at load, the normal missing-master behaviour, where 1.0.0 only left the option out of the card. The plugin keeps its name and still holds one quest record; its own record's load index moved from 01 to 02 on disk. Saves made with 1.0.0 are unaffected: the game finds the quest by plugin name and local id. The DLL's code is unchanged.

## 1.0.0 - 2026-11-03 - untested

### Added
- First version. Winter Cove joins Alternate Perspective's My own Home card. Choose it and the game begins inside Winter Cove through the cell's own arrival point, with the door key in your pack and no gold. The home has no sale, no sign and no Houses Owned count, so the start adds none. Its own door lock is left as it is: the key in your pack is what opens it. The plugin holds one quest record and edits nothing of Winter Cove's; without Winter Cove.esp the option is not offered.
