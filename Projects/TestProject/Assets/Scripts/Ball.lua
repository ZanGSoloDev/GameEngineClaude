local Ball = {}
function Ball:OnCollisionEnter(other)
	BallHits = (BallHits or 0) + 1
	BallHitName = other.name
end
return Ball
