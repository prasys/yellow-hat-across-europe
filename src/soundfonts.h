/* soundfonts.h - where a General MIDI SoundFont is looked for on Linux, in
 * order, when madeline.ini has no soundfont= line. Shared by the engine
 * (src/midi.c), which plays the music with it, and the Linux launcher
 * (launcher/linux), which shows which one will be used. Plain C, no
 * dependencies: the launcher includes it without the rest of src/. */
#ifndef MADELINE_SOUNDFONTS_H
#define MADELINE_SOUNDFONTS_H

static const char *const madeline_soundfont_paths[] = {
    "/usr/share/soundfonts/default.sf2",             /* Arch: a link to whichever is installed */
    "/usr/share/soundfonts/FluidR3_GM.sf2",          /* Arch soundfont-fluid, Fedora fluid-soundfont-gm */
    "/usr/share/sounds/sf2/FluidR3_GM.sf2",          /* Debian/Ubuntu fluid-soundfont-gm */
    "/usr/share/sounds/sf3/default-GM.sf3",          /* Debian/Ubuntu musescore-general-soundfont */
    "/usr/share/sounds/sf2/default-GM.sf2",
    "/usr/share/sounds/sf2/TimGM6mb.sf2",            /* Debian/Ubuntu timgm6mb-soundfont */
    "/usr/share/soundfonts/freepats-general-midi.sf2",
    "/usr/share/soundfonts/default.sf3",
    "/usr/local/share/soundfonts/default.sf2",
};
#define MADELINE_SOUNDFONT_COUNT (sizeof madeline_soundfont_paths / sizeof *madeline_soundfont_paths)

#endif
