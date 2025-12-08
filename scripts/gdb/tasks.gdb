target extended-remote :3333
set confirm off
set pagination off
set mem inaccessible-by-default off
monitor arm semihosting enable
monitor reset halt

break hrt_error

break TaskA
break TaskB

commands 1
  silent
  bt
  p/x (unsigned)g_error
  continue
end

commands 2
  silent
  printf " [%d] TaskA counter %d\n", (unsigned)g_tick, (unsigned)dbg_counterA
  continue
end

commands 3
  silent
  printf " [%d] TaskB counter %d\n", (unsigned)g_tick, (unsigned)dbg_counterB
  continue
end

continue
