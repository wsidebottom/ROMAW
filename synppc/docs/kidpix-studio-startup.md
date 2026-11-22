# Kid Pix Studio Deluxe startup check

This is the installed 68K suite, distinct from the PowerPC Kid Pix Deluxe 4
installer described in `kidpix-startup.md`.

The user's executable is
`C:/Users/William/.romaw/Apps/Kid Pix Studio Deluxe/Studio Stuff/Kid Pix`.
The check used Windows build `3da964f`, with the application's existing
preferences and no forced SystemVersion or heap overrides.

Direct startup initializes the Window Manager but stays on a blue desktop.
Debugger snapshots show repeated PBGetCatInfo calls from the application's
catalog-search fallback. This is active recursive enumeration, rather than
a CPU exception: it starts at C's root and enters unrelated system and tool
directories. The captured search criteria request type `APPL`, creator `Kid3`.
The suite's parent `Kid Pix Studio Deluxe` application has that identity;
the internal `Kid Pix` executable has creator `0x4b946430`.

Starting the parent `Kid Pix Studio Deluxe` application reaches its red suite
selection screen. Selecting Kid Pix invokes the Windows Open With chooser
instead of starting an emulated child application. `NewLaunch` currently
routes extended launches with `launchContinue` to `ROMlib_launch_native_app`,
which calls ShellExecuteA without distinguishing Macintosh applications from
native host applications. Macintosh `launchContinue` requests must preserve
the caller while launching another emulated application; the existing native
dispatch does not implement that behavior.

Neither path reached the drawing canvas. No drawing, save, sound, or printing
workflow was verified. The diagnostic processes were stopped after inspection.
This check did not change application files, preferences, or runtime code.

Local diagnostic artifacts are ignored under `build/`: `kidpix-catalog.log`
records catalog requests and the search criteria; `kidpix-studio-pb.log`
records the repeated trap; `kidpix-studio-launcher/` contains the launcher
trace. The original executable and its AppleDouble resource fork were used
directly throughout the check.

## Launcher handoff fix

The extended launch request was captured from the real suite launcher. It sets
`launchContinue` and has a null launchAppParameters pointer. ROMAW now checks
the target's Finder type and starts `APPL` targets in another ROMAW process,
preserving the calling application's memory and window. Host targets continue
through the native launcher. Mounted images and the existing ROMAW document
vector are retained; document paths keep their order. Unrecognized parameter
blocks and negative document counts return paramErr instead of being treated as
ROMAW's private document vector.

The rebuilt Windows launcher was checked manually: selecting Kid Pix starts a
separate ROMAW process for `Studio Stuff/Kid Pix`, and the suite launcher
remains open. The child still performs the volume-wide search described above;
no drawing canvas has been reached. This fixes the Windows Open With handoff,
not the remaining application-discovery problem or general cross-process Mac
Process Manager/AppleEvent support.

The ECF regression fixture intercepts process creation and native launching to
verify target routing, document order, mount inheritance, unchanged parent
heaps, launch failures, metadata/path failures and malformed parameter blocks.
The Windows build/staging and robustness suite passed.

## Installed Studio application patches

The installed Digital Puppets, Moopies, Stampimator and SlideShow applications
can be updated with `tools/patch-kidpix-studio.py`. Pass the installed `Studio
Stuff` directory; the default is a read-only preview, and `--apply` writes the
verified changes after saving SHA-256-named AppleDouble backups under
`build/kidpix-quit/backups`. Application binaries and CD assets remain local.

The tool changes the File menu to Quit and makes the existing picker-launch
routine return success without launching another application. The caller's save
decision and ordinary shutdown path remain intact. SlideShow's free-space
helper also uses a 64-bit product and saturates large byte counts at the signed
32-bit limit. Genuine results below that limit, including insufficient free
space, remain unchanged. Resource signatures are checked before writing;
duplicate localized resources and unrelated AppleDouble metadata are retained.

These apps use PBCatSearch during startup to locate the suite launcher. The
runtime implements real catalog enumeration, selection masks and ranges,
continuation, EOF and asynchronous completion. The apps' `0xFFFFFFFF` timeout
is accepted as an unsigned duration. The volume capability bit remains clear:
this implementation does not detect catalog changes between continuation calls.
The Windows `catalog-search` file suite checks traversal, bounds, continuation,
callback registers and the app-local free-space helper's 68K arithmetic.

The suite's additional pictures, sounds and puppet assets require its real CD
contents. Mount the existing Studio Deluxe image read-only; a local copy under
the user's `.romaw/Disks` avoids depending on a network share. Windows checks
reached all four editing screens and their Quit commands exited without opening
the picker. This does not establish full recording, movie or printing support.

## Multimedia work

The application has now also been observed showing its nonfatal missing-extension
warning for QuickTime 2.5, Sound Manager 3.2.1 and Speech Manager 1.5. This does
not establish that its drawing or multimedia workflows work.

Sound channel creation now allocates and initializes a usable channel in both
real and pretend modes, checks both allocation failures before publishing the
channel, and releases allocations on rollback. Channel status validates the
registered channel and output extent, initializes all fields and includes
double-buffer activity. Sound Manager status returns the packed six-byte record
with the actual registered channel count. Beep enable state now round-trips and
controls SysBeep. Pretend-mode disposal releases its registered channel too.

The previous placeholder Sound Manager version used an invalid release-stage
byte. The existing 3.0.3 baseline now uses final-release NumVersion encoding and
the normal numeric return convention. It has not been raised to 3.2.1 to suppress
Kid Pix's warning. Sound status and beep APIs are also exposed to PPC callers;
the real PPC sound probe checks the version through the bridge before testing
queued PCM playback and completion timing.

The sound-status fixture checks allocation rollback, caller-provided channel
preservation, all status fields and guards, invalid lengths and channel pointers,
double-buffer activity, channel counts, beep controls and pretend-mode disposal.
Windows build/staging, robustness, SDL playback, PCM conversion, MACE decoding
and the staged PPC sound probe passed. File playback, recording, speech and
QuickTime functionality remain incomplete.

## Speech synthesis foundation

Windows now supplies real local synthesis through SAPI. Native voice objects
remain outside guest memory; 68K SoundDispatch and PPC SpeechLib/InterfaceLib
calls share a bounded adapter. CountVoices, GetIndVoice, MakeVoiceSpec,
GetVoiceDescription, NewSpeechChannel/DisposeSpeechChannel, SpeakString,
SpeakText, SpeechBusy, StopSpeech, immediate PauseSpeechAt, ContinueSpeech and
speech rate controls are implemented. GetSpeechInfo/SetSpeechInfo cover volume,
rate and refcon; status and current voice can be queried. Voice descriptions use
the actual installed host voice name and a distinct SAPI identity. Default guest
speech uses the first enumerated host voice. No Apple voice assets are bundled.

Text is copied from validated MacRoman guest spans into native UTF-16 before
asynchronous synthesis. Text is treated literally, including XML-like input.
Volume reaches the synthesizer; disposal, application reset and process shutdown
release native objects. Invalid channels return invalidComponentID, and unsupported
speech information selectors return siUnknownInfoType. The guest adapter does
not yet advertise Speech Manager 1.5 or its Gestalt capability. Word/sentence
boundaries, callbacks, pitch, phonemes and dictionaries still need implementation.
This is not complete Kid Pix speech compatibility.

The intercepted guest test checks allocation rollback, spans, stale tokens,
68K Pascal argument order/result slots, voice records, status and scalar controls.
`system/tests/check-media.sh host-speech` separately exercises real Windows voices,
renders a disposable WAV and verifies nonzero 22050 Hz mono 16-bit PCM, with no
speaker playback. The original PPC Speech Probe exercises actual imports, voice
selection/description, channel creation, muted speech, completion polling and
cleanup through the staged ROMAW, with both JIT and interpreter execution.
Windows build/staging, full robustness and both repository formatting checks
passed. The Kid Pix warning and its multimedia workflows have not yet been
verified with this foundation enabled.

### Speech completion callbacks

Speech channels now retain their creator's A5 and support soSpeechDoneCallBack
and soCurrentA5. Completion is polled on the emulation thread from GetNextEvent
and SpeechBusy, never from a SAPI worker. The callback receives the guest channel
and refcon through CToPascalCall, supporting 68K code and PPC routine descriptors.
Delivery clears pending completion before calling guest code. A bounded snapshot
and allocation serials protect iteration when callbacks dispose channels, create
new ones, reset speech or poll recursively. Disposal cancels pending callbacks.

The guest fixture checks deferred delivery, exactly-once completion, A5
restoration, recursive polling, disposal and full subsystem reset during a callback. The staged PPC
probe installs an actual routine descriptor and waits for its callback through
WaitNextEvent; it checks the channel/refcon arguments and disposes the descriptor.
Windows build/staging, robustness and formatting checks passed. The staged
callback probe passed in both JIT and interpreter modes. Text-done, word and
phoneme callbacks are still outstanding.

### Text-buffer completion and continued speech

soTextDoneCallBack now supplies real guest-memory next-buffer, length and flags
outputs. Input is copied before notification, so the callback can release it.
A returned buffer is validated and appended to the existing native speech queue;
zero length ends the chain. Each event-loop poll services at most one text
callback per channel. Request generations prevent stale callback outputs from
replacing a new SpeakText or undoing StopSpeech. Returned buffers after a stop
are ignored, and invalid buffers/unsupported flags are reported by soErrors.
Normal and kNoSpeechInterrupt continuation are supported. Prosody suppression
and preflight-pause flags remain unsupported and record unimplMsg.

The guest fixture checks callback argument storage, chained input, invalid spans,
error retrieval/clearing and channel disposal during text completion. The staged
PPC probe installs a five-argument callback that queues a second buffer, observes
two text completions and a final speech completion. The native WAV test compares
single-buffer rendering with queued rendering to verify the second buffer extends
rather than replaces the first. This still does not establish Kid Pix's speech,
QuickTime, recording or file-playback workflows.

Validation for text continuation: the rebuilt/staged Windows executable passed
both JIT and interpreter versions of the chained-buffer callback probe. The
native WAV comparison, full robustness suite and repository formatting checks
passed; the focused guest test was rerun after the final status/error-record
adjustments. Speech status reports zero input bytes retained because the host
owns its copy, independently of whether playback has finished.

## Kid Pix menu icons

Windows CopyMask previously returned immediately when the specialized 1/8-bit
source-to-8-bit destination path declined a bitmap. Kid Pix's 4-bit cicn images
therefore disappeared, while its 8-bit Import/Export icons remained visible.
Unsupported fast-path depths now reach the existing region-based CopyBits path,
which handles palette conversion and mask scaling. The user's original File
menu was checked: New, Open, Save, Print and Return to Picker now draw their
color icons, and the startup warning's icon also appears.

The original PPC Icon Probe checks 2/4-bit cicn resources with sparse palettes,
scaled masks, red foreground pixels and untouched white gaps. Both JIT and
interpreter execution passed. Windows build/staging, SDL pixel checks and both
formatting checks passed. The separate whole-window refresh issue is still
under investigation; this icon fix does not establish that it is resolved.

## Focus/restore repaint recovery

SDL activation now repaints the full guest framebuffer on input-focus or
application-visibility gain, independently of guest drawing and SDL_VIDEOEXPOSE.
Mouse-only focus changes do not cause suspend/resume or a full repaint. Existing
clipboard conversion and guest suspend/resume notifications remain in order.
The activation fixture exercises the production handler with intercepted display
and clipboard calls, including unchanged/changed clipboard, focus loss, restore
and mouse-only changes.

The rebuilt Windows Kid Pix was tested with a visible pencil stroke: switching
to Basilisk II and back restored the canvas/tools, and a second stroke drew
correctly. Minimizing and restoring also recovered both strokes. Kid Pix briefly
shows its blue background while its own resume handler recreates the content;
the drawing then returns. This is a bounded manual check, not proof that every
focus-related hang is eliminated. The full robustness suite and formatting
checks passed, and the executable was staged. An earlier attached GDB session
left an INT3 at sendresumeevent after detach; its diagnostic process was stopped,
and these final checks ran fresh without a debugger.

### Preserve the inactive canvas

The previous repaint change still exposed Kid Pix's blue guest desktop while
inactive and during reactivation. SDL now keeps the last uploaded surface and
palette while the guest hides/recreates its windows. Exposes re-present that
surface. Palette writes are deferred and applied when presentation resumes.
Removing the guest resume event marks a pending release; the next event-loop
entry releases it after the application's handler returns. Peeking at events
or delivering an old resume while inactive does not release the saved frame.
Applications without suspend/resume support, or using NoSuspend, resume
presentation immediately. Replacing the video surface also releases the hold.

The focus fixture checks this event sequence and the immediate-release cases.
The presentation fixture checks sparse deferred palette updates, unchanged
colors while held, release and repeated transitions. The rebuilt Kid Pix kept
a test stroke/tools visible while Basilisk II was foreground, retained them
immediately after focus returned, and accepted a second stroke after resume.
This corrects the earlier overly broad report that repaint alone removed the
blue-background transition. Multimedia services remain incomplete.

## Sound channel pause and resume

Queued and immediate pause/resume commands now control actual channel processing.
Paused channels report paused/busy status and retain their current buffer position
while output time advances. Immediate commands release a queued pause as required
by the Sound Manager command model.

The staged Windows PCM/callback probe queues a pause before playback, checks that
no callbacks arrive during the pause, verifies channel status, then resumes and
checks all playback callbacks. JIT and interpreter runs passed. The focused
status/timeline fixture, full robustness suite, SDL playback regression and
repository formatting check passed; ROMAW was rebuilt and staged. This does
not implement file playback, recording, QuickTime or the remaining speech APIs.

## Sound amplitude and queue flushing

Queued and immediate ampCmd commands now set a per-channel amplitude from 0 to
255. The mixer scales that channel's unsigned PCM around silence before adding
it to other channels. This applies to standard, extended/compressed, generated
note and double-buffer playback; it preserves the current buffer and clock.
New channels start at full amplitude. Invalid immediate amplitudes return
paramErr without changing the gain.

An immediate flush now removes queued commands without stopping the active
buffer or resetting its sample position. quietCmd still stops the active sound
and leaves the queue available for subsequent processing.

The focused fixture checks all 256 amplitude values, silence, negative/positive
samples, another channel's existing mix, held boundary samples, invalid values,
and the distinct flush/quiet effects. The staged PCM probe also queues an
amplitude command before its sample buffers; JIT and interpreter runs passed.
Windows build/staging, full robustness, SDL audio and formatting checks passed.
This remains channel amplitude support; stereo volume controls and file playback
are separate outstanding work.
## SpeakBuffer text submission

SpeakBuffer is available through the 68K SoundDispatch selector and PPC native
speech imports. Flags zero use the existing SpeakText replacement path.
kNoSpeechInterrupt rejects input with synthNotReady while the prior text awaits
release, then appends copied text without purging existing audio. A zero-length
request stops the channel and cancels text continuation. Unsupported ending
prosody/preflight controls return unimplMsg, preserving the previous request.
The speech version and Gestalt advertisement are unchanged.

The guest fixture checks rejection without replacement, acceptance after text
release, unsupported flags, invalid extents, empty-input stop and the four-argument
Pascal stack/result layout. The staged PPC probe uses SpeakBuffer, verifies busy
and unsupported-flag errors, then receives both chained text-done callbacks and
one speech-done callback. JIT and interpreter runs passed. Native WAV synthesis
still produces nonzero PCM and a longer output when a second buffer is appended.
Windows build/staging, the focused guest fixture and repository formatting check
passed. These checks do not establish Kid Pix's complete speech workflow;
phonemes, dictionaries, boundary callbacks and other speech services remain.
## AIFF file playback

SndStartFilePlay now plays AIFF and AIFF-C (NONE, twos, sowt, MAC3, MAC6)
mono/stereo 8/16-bit PCM through the existing mono mixer. Signed AIFF samples
are converted correctly; chunk padding, SSND offsets, chunk order, sample rates
and all byte extents are checked before decoding. File marks are restored on
success and read/format failure. Selections use the documented fixed seconds
record, with unitTypeNoSelection support.

Synchronous calls support an internally allocated channel. Asynchronous calls
require a caller-owned channel, support pause/resume toggling, report busy state
and deliver the completion on the CPU thread after the final output quantum.
Immediate stop, wait-for-finish stop, channel disposal and application cleanup
release pending playback. Stop suppresses completion. Completion delivery removes
the old request before calling guest code, restores A5 and tolerates disposal or
recursive polling. Flush does not interrupt active file playback.

This implementation reads in bounded chunks but retains decoded PCM in host
memory before playback. It does not yet provide large-file streaming or all
AIFF-C codecs; unsupported codecs return explicit
errors. General QuickTime and recording compatibility remain incomplete.

Validation: the AIFF parser's ASan/UBSan fixture checks exact-size truncations,
malformed chunks/rates/counts, stereo conversion, signed 8-bit PCM and little-
endian AIFF-C. The Windows fixture checks PCM including the last sample, selected
frames, guarded input buffering, file marks, completion timing, pause, stop,
synchronous channel ownership, reset and disposal from a callback. The original
PPC Fileplay Probe opens an AIFF using guest File Manager calls and checks both
synchronous and asynchronous APIs, pause/status, completion, cancellation and
file-mark preservation. JIT and interpreter runs passed. Windows build/staging,
full robustness, generated-file, SDL audio and PowerShell formatting checks passed.
## Sound-resource file playback

SndStartFilePlay now also accepts fRefNum zero with a sound-resource ID. Its
bounded decoder accepts format 1/2 command tables with one relative bufferCmd
and optional null commands, standard unsigned 8-bit PCM, extended 8/16-bit PCM,
and MACE 3:1/6:1. Other command programs and corrupt headers return badFormat.
Resource handles remain locked only while their samples are copied/decoded,
and their original state is restored on success or failure. Playback retains
its own PCM copy, including when the original resource changes afterwards.
MACE selections decode preceding packets to retain predictor state.

The user's local KidPix Sounds and KidFlix Sounds contain 411 resources, all
using standard sampled-sound headers. An ignored local ASan/UBSan asset fixture
parsed every resource and verified 6,434,722 frames copied byte for byte.
These Apple assets were not committed. The maintained tests check format 2
and compressed-resource truncations, invalid offsets/counts, handle-state
restoration, copy ownership, missing resources and the existing independent
MACE reference hashes through the actual playback loader.

The staged PPC Fileplay Probe now plays a generated sound resource from its
application resource fork after its AIFF and cancellation checks. It waits for
actual completion with a deadline rather than assuming a fixed device-buffer
latency. Both JIT and interpreter runs passed. Windows build/staging, full
robustness, MACE reference checks and focused ASan/UBSan checks passed. This is
not a complete Kid Pix multimedia workflow test; QuickTime and recording remain
unsupported and Speech Manager coverage remains partial.
## Waiting channel disposal

SndDisposeChannel with quietNow false now queues quietCmd behind existing
commands and waits for it to execute before freeing the channel. Queued PCM
and callbacks finish in order; pending file playback reaches its normal
completion. Immediate disposal still flushes and stops. Channel registration is
checked before dereferencing the pointer, and a private disposing flag rejects
recursive disposal of the same channel during its callbacks.

The Windows fixture checks invalid references, PCM/callback draining, recursive
disposal rejection, immediate cancellation and file completion while disposal
waits. The staged PCM probe queues a final buffer/callback immediately before
waiting disposal and requires the callback count to reach 33 before return.
The pause check now waits for actual paused status with a deadline before
measuring the paused interval, avoiding device-startup latency assumptions.
PCM and file/resource probes passed under both JIT and interpreter execution.
Windows build/staging, full robustness and SDL sound checks passed. Full Kid Pix
multimedia compatibility remains open, particularly QuickTime, recording and
remaining Speech Manager services.
## MACE-compressed AIFF-C

The AIFF-C parser now accepts MAC3/MAC6 and validates encoded packet extents
before expanding their packet-frame counts to PCM-frame counts. The existing
MACE decoder has a stateful packet interface; its one-shot interface is retained.
File playback reads bounded groups of packets and carries predictor state across
reads. A later selection decodes preceding packets, then retains only selected
PCM, including selections that start/end between packet boundaries.

Windows fixtures check mono/stereo files, one-packet input buffers, guarded
storage, file-mark restoration, truncated payloads and selections against the
full decoded sound. MACE output hashes remain identical to the existing FFmpeg
reference values. Splitting the same stream into different chunk sizes also
produces identical output. Windows and Linux ASan/UBSan decoder checks, parser
bounds checks and full robustness passed. The staged Fileplay Probe plays both
compressed file types and completes under JIT and interpreter execution.
Decoded selected PCM is still retained in host memory before playback; bounded
input reads are not yet a complete streaming implementation.
## Silent audio regression output

The PPC regression runner now selects `-sdlaudio test`. This ROMAW-owned
output loop exercises the normal SDL callback/mixer handshake without opening a
host audio device. Set `ROMAW_TEST_AUDIO_CAPTURE` to capture raw unsigned
8-bit mono PCM at the rate reported in stderr. Ordinary application launches
still select their normal host output. Explicit unavailable SDL driver names
are rejected before opening an audio device; a successful SDL_AudioInit alone
does not establish that the requested driver exists.

The user reported harsh/corrupt sounds from the synthetic listening probes.
Those probes include pseudo-random compressed MACE packets and a constant-value
resource fixture, so they are unsuitable as listening samples. An attempted
SDL dummy-driver mute did not silence them because that driver is absent from
the local SDL build. Future automated probes use the explicit test output.

`check-runtime.ps1 fileplay-capture` checks synchronous/asynchronous PCM, resource
playback and MAC3/MAC6 output sample for sample after resampling. MACE reference
PCM is checked against the existing independent hashes. Every inter-clip gap
and the tail must be silence. Both JIT and interpreter captures passed. The SDL
fixture also forbids host-device opening and verifies capture through actual
initialization, playback and shutdown. Windows build/staging, robustness and
source/PowerShell format checks passed. These checks do not establish actual
Kid Pix listening quality or complete multimedia compatibility.
## Windows native-path startup and modal presentation

A normal native-path launch reproduced the blue-screen startup delay. Debugger
snapshots showed active recursive PBGetCatInfo enumeration of unrelated C-drive
folders. The profile was mounted using a DOS short name, while the
launch argument used the full profile directory name. The longest-prefix volume lookup consequently
classified the application under C rather than the ROMAW profile volume.
Launching the same application through its ROMAW: path reached the extension
warning promptly.

The legacy profile path now retains its actual spelling when representable in
the Windows code page; a short path remains a fallback for unrepresentable
Unicode names. The Windows profile fixture checks that the ordinary path is
preserved and still exercises Unicode profile migration. Windows and Linux
profile fixtures passed.

The warning also exposed a held-presentation problem after host focus changes.
Modal event loops can exclude osEvt and leave the queued resume unconsumed.
Foreground mouse/key input now releases a pending held surface and repaints it;
background input cannot release it, and ordinary later input does not trigger
repeated full repaints. The existing resume-event path remains available for
applications that handle those events. The SDL focus fixture covers both paths.

The rebuilt/staged executable was launched with the user's full Windows Kid Pix
path. Its extension warning appeared, dismissing it reached the drawing canvas,
and a pencil drag produced a visible stroke. The interactive test used silent
test audio. Full robustness, Windows/Linux profile and focused SDL tests passed,
as did source and PowerShell formatting checks. This verifies startup and basic
drawing, not full QuickTime, recording, speech or audio compatibility.
## Built-in multimedia reporting and Movie Toolbox foundation

At the user's request, ROMAW now reports built-in compatibility targets of
QuickTime 2.5, Sound Manager 3.2.1 and Speech Manager 1.5. These targets do not
require installing Apple's extension files. Speech presence is advertised on
the Windows/Syn68k build that supplies the SAPI backend. SpeechManagerVersion
works through both SoundDispatch's four-byte Pascal result slot and the PPC
SpeechLib/InterfaceLib bridge; the bridge no longer narrows that result to an
OSErr. Sound-off behavior remains unchanged. The installed Kid Pix 68K
application reached its drawing canvas without the extension warning.

This changes advertised compatibility targets, not the completeness of all
three APIs. Existing sound playback and host speech implementations remain in
use; unsupported speech/recording services still return their existing errors.
Do not describe the target versions as full Apple implementation equivalence.

The former Movie Toolbox success placeholders now have a bounded native
foundation for one self-contained raw 24-bit RGB video track. Atom/sample tables
validate all sibling extents, sample sizes, chunk mapping and durations before
publishing a movie. Frames must remain within the media-data atom. Files are
limited to 64 MiB, 65,536 samples and 2048 by 2048 pixels. Duplicate media or
track atoms and unsupported codecs are rejected rather than silently omitted.
Movie-owned input survives closing its file; file marks are restored on load
success and failure. Native state stays outside guest memory, with a validated
guest token and launch-reset disposal.

MoviesTask/UpdateMovie render frames through a 32-bit GWorld and CopyBits.
Start/stop, preroll, rewind, activation, bounds and completion use a TickCount
media timeline, including wraparound. InterfaceLib and QuickTimeLib share these
built-in exports. Supporting PPC GWorld calls are now native imports too.
Movie controllers, compressed video codecs, audio tracks, resource-fork movies,
editing/export and the broader QuickTime component APIs remain unfinished.

The parser fixture passes ASan/UBSan checks for every truncated prefix and
single-byte corruption. The Windows runtime fixture checks exact ARGB output,
file ownership/marks, allocation/read rollback, timing/wrap, stop/resume,
inactive drawing suppression and stale-token disposal. The original staged PPC
movie probe reads back red, green and blue pixels using GetCPixel after closing
the source file, and checks completion and Gestalt reporting. The sound and
muted speech probes check the new versions through real guest imports. These
checks cover the implemented foundation, not a complete Kid Pix multimedia
workflow.
Validation for this batch: Windows build/staging, full robustness, parser
ASan/UBSan, focused movie runtime tests, all 29 PPC probes under JIT, and the
sound/speech/movie probes under the interpreter passed. Changed C/C++ and
Python formatting and PowerShell checks passed. The full source-format check
reported one concurrently edited file, runtime/main.c; that unrelated work was
left untouched.
