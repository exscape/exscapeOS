for i = 0, 1000000, 1 do
  a = math.random()
  b = math.random()
  ans = {}

  for j = 0, 100, 1 do
    ans[j] = a + b
  end

  for j = 1, 100, 1 do
    if ans[j] ~= ans[0] then
      print ("CALCULATION ERROR")
	  os.exit(1)
	end
  end
end

print ("Done")
