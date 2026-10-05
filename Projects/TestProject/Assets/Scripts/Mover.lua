-- Moves a kinematic body back and forth along an axis.
local Mover = {}
Mover.properties = { amplitude = 3.0, speed = 0.8 }

function Mover:OnCreate()
	self.origin = self.entity.transform.position
end

function Mover:OnUpdate(dt)
	local offset = math.sin(Time.time * self.speed) * self.amplitude
	self.entity.transform.position = self.origin + Vec3(offset, 0, 0)
end

return Mover
