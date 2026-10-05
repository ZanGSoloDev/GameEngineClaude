-- Self-destroying projectile.
local Bullet = {}
Bullet.properties = { lifetime = 3.0 }

function Bullet:OnCreate()
	self.age = 0
end

function Bullet:OnUpdate(dt)
	self.age = self.age + dt
	if self.age >= self.lifetime then
		self.entity:Destroy()
	end
end

function Bullet:OnCollisionEnter(other)
	BulletHits = (BulletHits or 0) + 1
end

return Bullet
