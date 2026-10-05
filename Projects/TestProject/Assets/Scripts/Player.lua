-- Interactive demo: WASD pushes the ball, Space shoots a bullet prefab, R resets, Escape quits.
local Player = {}
Player.properties = { force = 18.0, shootSpeed = 14.0 }

function Player:OnCreate()
	self.body = self.entity:GetComponent("RigidBody")
	self.cooldown = 0
end

function Player:OnUpdate(dt)
	local push = Vec3(0, 0, 0)
	if Input.IsKeyDown("W") then push = push + Vec3(0, 0, -1) end
	if Input.IsKeyDown("S") then push = push + Vec3(0, 0, 1) end
	if Input.IsKeyDown("A") then push = push + Vec3(-1, 0, 0) end
	if Input.IsKeyDown("D") then push = push + Vec3(1, 0, 0) end
	if push:LengthSquared() > 0 then
		self.body:AddForce(push:Normalized() * self.force)
	end

	self.cooldown = self.cooldown - dt
	if Input.IsKeyPressed("Space") and self.cooldown <= 0 then
		local position = self.entity.transform.worldPosition + Vec3(0, 1.2, 0)
		local bullet = Scene.Instantiate("Prefabs/Bullet.sfprefab", position)
		if bullet then
			bullet:GetComponent("RigidBody").velocity = Vec3(0, 5, -self.shootSpeed)
			Audio.PlayOneShot("Audio/beep.wav", position, 0.4)
		end
		self.cooldown = 0.25
	end

	if Input.IsKeyPressed("R") then
		self.entity.transform.position = Vec3(0, 1, 4)
	end
	if Input.IsKeyPressed("Escape") then
		Application.Quit()
	end

	local camera = Scene.GetMainCamera()
	if camera then
		local target = self.entity.transform.worldPosition
		camera.transform.position = target + Vec3(0, 5, 9)
		camera.transform:LookAt(target)
	end
end

return Player
