@tool
extends RefCounted
class_name QuebratskSoundLoader

## Turning a sound in the VFS into something Godot can play.
##
## Lives on its own because two very different callers need it: the dock, to preview a
## sound, and anything built on top of the importer that wants to hear one. Copying the
## RIFF walk into each of them is how a parser ends up with two versions that disagree.
##
## Only WAV needs decoding here. Godot ships importers for the other two and they take the
## file's own bytes, so handing those straight over is simpler and lossless.


## `looping` is for sounds meant to run for as long as a scene is open, which a map's
## ambience is and a gunshot is not. It has to be asked for rather than read from the file:
## these WAVs carry loop points in a "cue " chunk that the games honour by convention, and a
## great many that plainly loop in game do not have one.
static func load_sound(vfs: VFSManager, uri: String, looping := false) -> AudioStream:
	match uri.get_extension().to_lower():
		"wav":
			return load_wav(vfs, uri, looping)
		"mp3":
			var mp3 := AudioStreamMP3.new()
			mp3.data = vfs.read_file(uri)
			return mp3 if mp3.data.size() > 0 else null
		"ogg":
			var bytes := vfs.read_file(uri)
			return AudioStreamOggVorbis.load_from_buffer(bytes) if bytes.size() > 0 else null
	return null


## Build a playable stream from a .wav in the VFS.
##
## Godot can only load audio off disk, and these files live inside an archive that is never
## extracted, so the container is parsed here and the samples handed over directly. RIFF is
## a chunk list, not a fixed header: the fmt and data chunks are found by walking it,
## because LIST and fact chunks sit between them in plenty of real files.
static func load_wav(vfs: VFSManager, uri: String, looping := false) -> AudioStreamWAV:
	var bytes := vfs.read_file(uri)
	if bytes.size() < 44:
		return null
	if bytes.slice(0, 4).get_string_from_ascii() != "RIFF":
		return null
	if bytes.slice(8, 12).get_string_from_ascii() != "WAVE":
		return null

	var channels := 0
	var rate := 0
	var bits := 0
	var samples := PackedByteArray()

	var pos := 12
	while pos + 8 <= bytes.size():
		var id := bytes.slice(pos, pos + 4).get_string_from_ascii()
		var size := bytes.decode_u32(pos + 4)
		var body := pos + 8
		# A declared size past the end means the file is truncated; stop rather than
		# slicing beyond the buffer.
		if size > bytes.size() - body:
			break
		if id == "fmt ":
			channels = bytes.decode_u16(body + 2)
			rate = bytes.decode_u32(body + 4)
			bits = bytes.decode_u16(body + 14)
		elif id == "data":
			samples = bytes.slice(body, body + size)
		pos = body + size + (size & 1)  # chunks are word-aligned

	if samples.is_empty() or rate <= 0 or channels <= 0:
		return null

	var stream := AudioStreamWAV.new()
	match bits:
		8:
			# RIFF stores 8-bit samples unsigned, 0 to 255, and Godot's FORMAT_8_BITS wants
			# them signed, -128 to 127. Handing the bytes over untouched inverts every
			# sample around the midpoint: it plays at full amplitude as a saturated blast
			# rather than as the sound. Most GoldSrc audio is 8-bit, so this was almost
			# everything Half-Life and Counter-Strike have.
			stream.format = AudioStreamWAV.FORMAT_8_BITS
			for i in samples.size():
				samples[i] = (samples[i] + 128) & 0xFF
		16:
			stream.format = AudioStreamWAV.FORMAT_16_BITS
		_:
			return null  # ADPCM and float WAVs are not handled
	stream.mix_rate = rate
	stream.stereo = channels >= 2
	stream.data = samples

	if looping:
		# The whole sample. loop_end is in frames, and a frame is one sample per channel, so
		# leaving it at zero would loop a fraction of a second of a stereo file.
		var bytes_per_frame: int = channels * (1 if bits == 8 else 2)
		stream.loop_mode = AudioStreamWAV.LOOP_FORWARD
		stream.loop_begin = 0
		stream.loop_end = samples.size() / maxi(bytes_per_frame, 1)
	return stream
