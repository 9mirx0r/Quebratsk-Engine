extends Node

## Where a sound is heard from, rather than merely how loud it is.
##
## Distance alone is not what tells you a thunderclap happened outside while you are indoors.
## A wall between you and it takes the top off the sound and leaves the low end, and the room
## you are standing in adds its own reflections to whatever arrives. Godot gives distance for
## free through AudioStreamPlayer3D and neither of the other two.
##
## Two buses do most of it:
##
##     Rooms      the reverberation of wherever the listener is standing
##     Occluded   the same, with the high end taken off and the level dropped
##
## A tracked sound is moved between them by a ray from it to the listener. That test is the
## game's own: FEnvSoundInRange in ReGameDLL's sound.cpp traces from an env_sound to the
## player and gives up if anything is in the way, which is how GoldSrc decided whether you
## could hear a room at all.
##
## What this is not: a simulation. Real occlusion is a diffraction problem and sound goes
## round corners; a single ray says blocked or not blocked. It is the difference between
## "outside" and "outside, heard from in here", which is the difference worth having.

## How much a wall takes off, in decibels, and how far down the frequency it cuts.
const OCCLUDED_DROP := -11.0
const OCCLUDED_CUTOFF := 900.0

## How quickly a sound crosses between the two, in seconds. Instant switching clicks audibly
## when a listener walks past a doorway.
const BLEND := 0.18

var _rooms_bus := -1
var _occluded_bus := -1
var _listener: Node3D
var _tracked: Array = []


## Build the two buses. Returns the name of the one an unobstructed sound belongs on.
func setup(listener: Node3D) -> String:
	_listener = listener

	_rooms_bus = AudioServer.bus_count
	AudioServer.add_bus(_rooms_bus)
	AudioServer.set_bus_name(_rooms_bus, "Rooms")
	AudioServer.set_bus_send(_rooms_bus, "Master")

	_occluded_bus = AudioServer.bus_count
	AudioServer.add_bus(_occluded_bus)
	AudioServer.set_bus_name(_occluded_bus, "Occluded")
	# Through Rooms, so something heard through a wall still picks up the reverberation of the
	# room it is heard in. That is the whole point: thunder outdoors, heard from inside.
	AudioServer.set_bus_send(_occluded_bus, "Rooms")
	AudioServer.set_bus_volume_db(_occluded_bus, OCCLUDED_DROP)

	var muffle := AudioEffectLowPassFilter.new()
	muffle.cutoff_hz = OCCLUDED_CUTOFF
	muffle.resonance = 0.2
	AudioServer.add_bus_effect(_occluded_bus, muffle)
	return "Rooms"


## Follow this sound: it will be moved between the two buses as things get between it and the
## listener. Meant for sounds that come from a place — a gunshot, thunder, a dripping pipe —
## and not for anything that is meant to be heard regardless.
func track(sound: AudioStreamPlayer3D) -> void:
	if sound == null or _rooms_bus < 0:
		return
	sound.bus = "Rooms"
	_tracked.append(sound)


## The reverberation of the room the listener is in.
##
## Replaced rather than adjusted: an AudioEffectReverb reads its parameters when it is added
## to a bus, so sliding one into another does not work and swapping is both simpler and
## cleaner to hear.
func set_room(reverb: AudioEffectReverb) -> void:
	if _rooms_bus < 0:
		return
	while AudioServer.get_bus_effect_count(_rooms_bus) > 0:
		AudioServer.remove_bus_effect(_rooms_bus, 0)
	if reverb != null:
		AudioServer.add_bus_effect(_rooms_bus, reverb)


func _physics_process(delta: float) -> void:
	if _listener == null or not is_instance_valid(_listener) or _tracked.is_empty():
		return

	var space := _listener.get_world_3d().direct_space_state
	var ear := _listener.global_position

	for i in range(_tracked.size() - 1, -1, -1):
		var sound = _tracked[i]
		if not is_instance_valid(sound):
			_tracked.remove_at(i)
			continue
		# Silent sounds cost nothing to leave alone, and a level can hold dozens of them.
		if not sound.playing:
			continue

		var query := PhysicsRayQueryParameters3D.create(sound.global_position, ear)
		query.collide_with_areas = false
		var blocked := not space.intersect_ray(query).is_empty()

		var wanted: StringName = &"Occluded" if blocked else &"Rooms"
		if sound.bus != wanted:
			sound.bus = wanted
