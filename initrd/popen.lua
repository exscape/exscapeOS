local file = assert(io.popen('/bin/ls -la', 'r'))
local output = file:read('*all')
file:close()
print(output) --> Prints the output of the command.
