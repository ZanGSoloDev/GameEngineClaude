-- Rotates the entity around Y. Exported property: speed (degrees per second).
local Spinner = {}
Spinner.properties = { speed = 45.0 }

function Spinner:OnUpdate(dt)
	self.entity.transform:Rotate(Vec3(0, self.speed * dt, 0))
end

return Spinner
