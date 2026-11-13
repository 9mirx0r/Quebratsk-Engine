extends RefCounted

## Walking up a step instead of into it.
##
## GoldSrc lifts anybody walking into something up to 18 units high onto the top of it: the
## move is made twice, once as it is and once from that much higher, and the one that got
## further is kept. That is sv_stepsize, and PM_WalkMove in the public SDK's pm_shared.c. A
## body in Godot has nothing of the kind and stops dead against the first stair.

## sv_stepsize, in metres.
const HEIGHT := 18.0 * 0.0254

## How far ahead a step is looked for, in metres. About the radius of whoever is walking.
const REACH := 0.3


## Move `body` for this tick, up a step if one is in its way. This replaces the call to
## move_and_slide. `grounded` says whether it was standing when the tick began, since nobody
## climbs a stair in mid air. Returns how far up it went, zero when there was no step.
##
## The speed it came in with survives the step. Stopping against the riser first and
## climbing on the next tick, which is what this did before, took all of it away at every
## single stair and made a staircase something to be fought up.
static func move(body: CharacterBody3D, delta: float, grounded: bool) -> float:
	var from := body.global_transform
	var before := body.velocity
	var flat := Vector3(before.x, 0.0, before.z)
	body.move_and_slide()
	if not grounded or before.y > 0.01 or flat.length() < 0.05 or not body.is_on_wall():
		return 0.0

	var plain := body.global_position - from.origin
	plain.y = 0.0

	# Up, as far as the ceiling lets it.
	var hit := KinematicCollision3D.new()
	var rise := HEIGHT
	if body.test_move(from, Vector3(0, HEIGHT, 0), hit):
		rise = hit.get_travel().y
	if rise < 0.02:
		return 0.0
	var raised := from.translated(Vector3(0, rise, 0))

	# Is it a step at all? Look a good way past it from up there, further than one tick goes:
	# a rounded body that has only just touched the corner of a stair comes straight back down
	# beside it, and looks exactly like one that walked into a wall.
	var reach := _slide(body, raised, flat.normalized() * maxf(flat.length() * delta, REACH))
	if reach.length() < 0.01:
		return 0.0
	if not body.test_move(raised.translated(reach), Vector3(0, -rise, 0), hit):
		return 0.0
	if rise + hit.get_travel().y < 0.01:
		return 0.0      # nothing higher over there: a wall, not a step
	if hit.get_normal().y < 0.7 and not _is_tread(body, hit.get_position(), reach.normalized()):
		return 0.0

	# It is. Make this tick's move from up there and come down wherever that leaves it, which
	# to begin with is on the corner; the ticks after carry it over.
	var travel := _slide(body, raised, flat * delta)
	if travel.length() + 0.001 < plain.length():
		return 0.0
	var ahead := raised.translated(travel)
	var drop := Vector3(0, -rise, 0)
	if body.test_move(ahead, drop, hit):
		drop = hit.get_travel()
	body.global_position = ahead.origin + drop
	body.velocity = flat
	return maxf(rise + drop.y, 0.001)


## How far a level move from `from` gets, turning along the first wall it meets instead of
## stopping at it. Going up a staircase with a shoulder against its side wall is the ordinary
## way of going up one.
static func _slide(body: CharacterBody3D, from: Transform3D, motion: Vector3) -> Vector3:
	var hit := KinematicCollision3D.new()
	if not body.test_move(from, motion, hit):
		return motion
	var done := hit.get_travel()
	done.y = 0.0
	var wall := hit.get_normal()
	wall.y = 0.0
	if wall.length() < 0.01:
		return done
	var rest := (motion - done).slide(wall.normalized())
	rest.y = 0.0
	if rest.length() < 0.001:
		return done
	if body.test_move(from.translated(done), rest, hit):
		rest = hit.get_travel()
		rest.y = 0.0
	return done + rest


## Whether what it came down on is flat ground. A rounded body lands on the corner of a stair
## before it lands on the stair, and the corner alone looks like a slope too steep to stand
## on; what is just past the corner settles it.
static func _is_tread(body: CharacterBody3D, at: Vector3, forward: Vector3) -> bool:
	var start := at + forward * 0.02 + Vector3(0, 0.08, 0)
	var query := PhysicsRayQueryParameters3D.create(start, start + Vector3(0, -0.16, 0),
			body.collision_mask, [body.get_rid()])
	var found := body.get_world_3d().direct_space_state.intersect_ray(query)
	return not found.is_empty() and (found["normal"] as Vector3).y >= 0.7
