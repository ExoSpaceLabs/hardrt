target extended-remote :3333
set confirm off
set pagination off
set mem inaccessible-by-default off
monitor arm semihosting enable
monitor reset halt

break SysTick_Handler
break PendSV_Handler

commands 1
  silent
  printf "Hit ------------------ SysTick_Handler breakpoint.\n"
  continue
end

commands 1
  silent
  printf "Hit ------------------ PendSV_Handler breakpoint.\n"
  continue
end

continue