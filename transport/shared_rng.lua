local simulationFrame = ...
local localRandom = math.random
local mask = 0xffffffff

local function mix(state, value)
    local word = (state ~ value) & mask
    word = ((word ~ (word >> 16)) * 0x7feb352d) & mask
    word = ((word ~ (word >> 15)) * 0x846ca68b) & mask
    return word ~ (word >> 16)
end

local function float(stream, entitySeed, actorSeed, sample)
    local frame = simulationFrame()
    if frame == nil then return nil end

    local game = Game()
    local seed = mix(game:GetSeeds():GetStartSeed(), #stream)
    for i = 1, #stream do
        seed = mix(seed, stream:byte(i))
    end
    seed = mix(seed, game:GetRoom():GetSpawnSeed())
    seed = mix(seed, frame)
    seed = mix(seed, entitySeed)
    seed = mix(seed, actorSeed)
    return mix(seed, sample or 0) / 4294967296.0
end

BetterMultiplayerRng = {
    Float = float,
    TimeMachine = function(machineSeed, playerSeed)
        return float("TimeMachine:updates", machineSeed, playerSeed) or localRandom()
    end,
}
