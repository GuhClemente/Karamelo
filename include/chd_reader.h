#ifndef CHD_READER_H_INCLUDED
#define CHD_READER_H_INCLUDED

// Teaches rcheevos to read compressed disc images.
//
// Without this every .chd came back "not in the database": RetroAchievements
// identifies a disc by reading files inside it, and its built-in reader only
// understands uncompressed cue/bin/iso.
//
// Call once, before any game is loaded. Non-CHD paths keep using the built-in
// reader, so nothing that already worked changes.
void ChdReaderInstall();

#endif
