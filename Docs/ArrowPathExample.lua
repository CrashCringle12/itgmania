-- Load as a foreground actor on an ordinary ITGmania chart.
-- Draw a fading guide along each active note column.
return Def.Actor {
  OnCommand = function(self)
    GAMESTATE:ForceSmoothLines(0)
    local screen = SCREENMAN:GetTopScreen()
    local columns = GAMESTATE:GetCurrentStyle():ColumnsPerPlayer()
    for _, pn in ipairs(GAMESTATE:GetEnabledPlayers()) do
      local player = screen:GetChild('Player' .. ToEnumShortString(pn))
      if player then
        player:SetArrowPathBlendMode('add')
        for col = 0, columns - 1 do
          player:SetNumPathGradientPoints(col, 4)
          player:SetPathGradientColor(0, col, 1, 0, 0, 1)
          player:SetPathGradientColor(1, col, 1, .8, 0, .8)
          player:SetPathGradientColor(2, col, .4, 1, 0, .6)
          player:SetPathGradientColor(3, col, .4, 1, 0, 0)
          player:SetPathGradientPoint(0, col, 0)
          player:SetPathGradientPoint(1, col, 2)
          player:SetPathGradientPoint(2, col, 4)
          player:SetPathGradientPoint(3, col, 6)
        end
        GAMESTATE:GetPlayerState(pn):GetPlayerOptions('ModsLevel_Song'):FromString(
          '*-1 100% arrowpath, *-1 1000% arrowpathgirth, *-1 -100% arrowpathdrawsizeback')
      end
    end
  end,
  OffCommand = function(self) GAMESTATE:ForceSmoothLines(-1) end
}
