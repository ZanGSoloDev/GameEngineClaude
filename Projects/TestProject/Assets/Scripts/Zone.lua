local Zone = {}
function Zone:OnTriggerEnter(other) TriggerEnters = (TriggerEnters or 0) + 1 end
function Zone:OnTriggerExit(other) TriggerExits = (TriggerExits or 0) + 1 end
return Zone
