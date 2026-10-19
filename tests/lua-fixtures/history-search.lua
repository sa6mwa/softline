local softline = require('softline')
local sl = softline.new()
assert(not sl:history_search_active())
assert(sl:history_add('match'))
local expected = { true, true, false }
local enters, cancels = 0, 0
assert(sl:bind_key(softline.KEY_ENTER, function()
  enters = enters + 1
  assert(sl:history_search_active() == expected[enters])
  return softline.KEY_ACTION_PASS
end))
assert(sl:bind_key(7, function()
  assert(sl:history_search_active())
  cancels = cancels + 1
  return softline.KEY_ACTION_PASS
end))
assert(sl:readline('> ') == 'draft!')
assert(enters == 3 and cancels == 2 and not sl:history_search_active())
sl:close()
print('SEARCH-QUERY-PASS')
