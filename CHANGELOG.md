# AP Home Start - Winter Cove - changelog

## 2.0.0 - 2026-11-04 - untested

### Changed
- The log no longer prints a sale sign of 00000000: Winter Cove is never sold, so it now says the home has no sale sign to remove.
- The plugin now has Winter Cove.esp as a master, so it cannot load without Winter Cove and must load below it. Without Winter Cove the game stops at load, the normal missing-master behaviour, where 1.0.0 only left the option out of the card. The plugin keeps its name and still holds one quest record; its own record's load index moved from 01 to 02 on disk. Saves made with 1.0.0 are unaffected: the game finds the quest by plugin name and local id. The DLL's code is unchanged.

### Fixed
- **Winter Cove now appears once in Alternate Perspective's menu, can be clicked, and sits in the My own Home card.** Alternate Perspective 4.1 puts its own None and Random cards in the menu the moment it opens and reads every start file afterwards; the mod took those two cards for a finished menu, found no My own Home card yet, and added a card of its own before Alternate Perspective had built its list - so the card was drawn twice (once before None, once after Random) and, lacking the enabled flag Alternate Perspective gives each entry, faded and refused the click. The mod now waits until Alternate Perspective has handed its finished list to the menu, joins the My own Home card as intended, and when a version has no such card adds one card of its own, once, marked enabled. Reported on Nexus (AE 1.6.1170, Alternate Perspective 4.1).

## 1.0.0 - 2026-11-03 - untested

### Added
- First version. Winter Cove joins Alternate Perspective's My own Home card. Choose it and the game begins inside Winter Cove through the cell's own arrival point, with the door key in your pack and no gold. The home has no sale, no sign and no Houses Owned count, so the start adds none. Its own door lock is left as it is: the key in your pack is what opens it. The plugin holds one quest record and edits nothing of Winter Cove's; without Winter Cove.esp the option is not offered.
