# AntiBlock
A simple solution to the player blocking problem. +USE players to swap places with them.

# CVars 
`as_command antiblock.disabled` disables the plugin  
`as_command antiblock.cooldown` controls how long swappers have to wait to swap again (or to be swapped with). The default is 0.6 seconds.   
`as_command antiblock.stomp` controls player stomping logic (falling on top of a player):  
- 0 = disabled 
- 1 = split damage evenly across all players involved (default)
- 2 = split damage across only the players that got stomped on  
- 3 = apply full damage to all players involved
