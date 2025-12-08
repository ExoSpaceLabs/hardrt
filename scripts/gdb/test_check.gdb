target extended-remote :3333
set confirm off
set pagination off
set mem inaccessible-by-default off
monitor arm semihosting enable
monitor reset halt

break hrt_error
break hrt__make_ready
break hrt__schedule
break TaskA
break TaskB

break PendSV_Handler

commands 1
  silent
  printf " >> hrt_error\n"
  printf " >> dbg_curr_sp=0x%08lx -- queue count: %d\n -- tick count: %d\n", (unsigned)dbg_curr_sp, (unsigned)dbg_tsk_q, (unsigned)g_tick
  printf " >> dbg_make_ready_id: %d dbg_make_ready_state: %d\n", (unsigned)dbg_make_ready_id, (unsigned)dbg_make_ready_state
  printf " >> dbg_pick: %d\n", (unsigned)dbg_pick
  printf " >> dbg_id_save: %d sp=0x%08lx\n", (unsigned)dbg_id_save, (unsigned)dbg_sp_save
  printf " >> dbg_id_load: %d sp=0x%08lx\n", (unsigned)dbg_id_load, (unsigned)dbg_sp_load
  printf " >> dbg_tasks_returned: %d\n", (unsigned)dbg_tasks_returned
  printf " >> g_current: %d\n", (unsigned)g_current
  printf " >> dbg_pend_calls: %d\n", (unsigned)dbg_pend_calls
  printf " >> dbg_pend_from_tick: %d\n", (unsigned)dbg_pend_from_tick
  printf " >> dbg_pend_from_core: %d\n", (unsigned)dbg_pend_from_core
  printf " >> dbg_pend_from_cortexm: %d\n", (unsigned)dbg_pend_from_cortexm
  printf " >> dbg_pend_from_tramp: %d\n", (unsigned)dbg_pend_from_tramp
  printf " >> dbg_basperi: %d\n", (unsigned)dbg_basperi
  printf "     TaskA counter %d\n", (unsigned)dbg_counterA
  printf "     TaskB counter %d\n", (unsigned)dbg_counterB
  printf " >>\n"
  bt
  p/x (unsigned)g_error
  continue
end


commands 2
  silent
  printf " >> hrt__make_ready\n"
  printf " >> dbg_curr_sp=0x%08lx -- queue count: %d\n -- tick count: %d\n", (unsigned)dbg_curr_sp, (unsigned)dbg_tsk_q, (unsigned)g_tick
  printf " >> dbg_make_ready_id: %d dbg_make_ready_state: %d\n", (unsigned)dbg_make_ready_id, (unsigned)dbg_make_ready_state
  printf " >> dbg_pick: %d\n", (unsigned)dbg_pick
  printf " >> dbg_id_save: %d sp=0x%08lx\n", (unsigned)dbg_id_save, (unsigned)dbg_sp_save
  printf " >> dbg_id_load: %d sp=0x%08lx\n", (unsigned)dbg_id_load, (unsigned)dbg_sp_load
  printf " >> dbg_tasks_returned: %d\n", (unsigned)dbg_tasks_returned
  printf " >> g_current: %d\n", (unsigned)g_current
  printf " >> dbg_pend_calls: %d\n", (unsigned)dbg_pend_calls
  printf " >> dbg_pend_from_tick: %d\n", (unsigned)dbg_pend_from_tick
  printf " >> dbg_pend_from_core: %d\n", (unsigned)dbg_pend_from_core
  printf " >> dbg_pend_from_cortexm: %d\n", (unsigned)dbg_pend_from_cortexm
  printf " >> dbg_pend_from_tramp: %d\n", (unsigned)dbg_pend_from_tramp
  printf " >> dbg_basperi: %d\n", (unsigned)dbg_basperi
  printf "     TaskA counter %d\n", (unsigned)dbg_counterA
  printf "     TaskB counter %d\n", (unsigned)dbg_counterB
  printf " >>\n"
  continue
end

commands 3
  silent
  printf " >> hrt__schedule\n"
  printf " >> dbg_curr_sp=0x%08lx -- queue count: %d\n -- tick count: %d\n", (unsigned)dbg_curr_sp, (unsigned)dbg_tsk_q, (unsigned)g_tick
  printf " >> dbg_make_ready_id: %d dbg_make_ready_state: %d\n", (unsigned)dbg_make_ready_id, (unsigned)dbg_make_ready_state
  printf " >> dbg_pick: %d\n", (unsigned)dbg_pick
  printf " >> dbg_pend_calls: %d\n", (unsigned)dbg_pend_calls
  printf " >> dbg_id_save: %d sp=0x%08lx\n", (unsigned)dbg_id_save, (unsigned)dbg_sp_save
  printf " >> dbg_id_load: %d sp=0x%08lx\n", (unsigned)dbg_id_load, (unsigned)dbg_sp_load
  printf " >> dbg_tasks_returned: %d\n", (unsigned)dbg_tasks_returned
  printf " >> g_current: %d\n", (unsigned)g_current
  printf " >> dbg_pend_calls: %d\n", (unsigned)dbg_pend_calls
  printf " >> dbg_pend_from_tick: %d\n", (unsigned)dbg_pend_from_tick
  printf " >> dbg_pend_from_core: %d\n", (unsigned)dbg_pend_from_core
  printf " >> dbg_pend_from_cortexm: %d\n", (unsigned)dbg_pend_from_cortexm
  printf " >> dbg_pend_from_tramp: %d\n", (unsigned)dbg_pend_from_tramp
  printf " >> dbg_basperi: %d\n", (unsigned)dbg_basperi
  printf "     TaskA counter %d\n", (unsigned)dbg_counterA
  printf "     TaskB counter %d\n", (unsigned)dbg_counterB
  printf " >>\n"
  continue
end


commands 4
  silent
  printf "     TaskA:\n"
  printf " >> dbg_basperi: %d\n", (unsigned)dbg_basperi
  printf "     TaskA counter %d\n", (unsigned)dbg_counterA
  printf "     TaskB counter %d\n", (unsigned)dbg_counterB
  continue
end

commands 5
  silent
  printf "     TaskB:\n"
  printf " >> dbg_basperi: %d\n", (unsigned)dbg_basperi
  printf "     TaskA counter %d\n", (unsigned)dbg_counterA
  printf "     TaskB counter %d\n", (unsigned)dbg_counterB
  continue
end

commands 6
  silent
  printf " >> PendSV_Handler\n"
  printf " >> dbg_curr_sp=0x%08lx -- queue count: %d\n -- tick count: %d\n", (unsigned)dbg_curr_sp, (unsigned)dbg_tsk_q, (unsigned)g_tick
  printf " >> dbg_make_ready_id: %d dbg_make_ready_state: %d\n", (unsigned)dbg_make_ready_id, (unsigned)dbg_make_ready_state
  printf " >> dbg_pick: %d\n", (unsigned)dbg_pick
  printf " >> dbg_id_save: %d sp=0x%08lx\n", (unsigned)dbg_id_save, (unsigned)dbg_sp_save
  printf " >> dbg_id_load: %d sp=0x%08lx\n", (unsigned)dbg_id_load, (unsigned)dbg_sp_load
  printf " >> dbg_tasks_returned: %d\n", (unsigned)dbg_tasks_returned
  printf " >> g_current: %d\n", (unsigned)g_current
  printf " >> dbg_pend_calls: %d\n", (unsigned)dbg_pend_calls
  printf " >> dbg_pend_from_tick: %d\n", (unsigned)dbg_pend_from_tick
  printf " >> dbg_pend_from_core: %d\n", (unsigned)dbg_pend_from_core
  printf " >> dbg_pend_from_tramp: %d\n", (unsigned)dbg_pend_from_tramp
  printf " >> dbg_basperi: %d\n", (unsigned)dbg_basperi
  printf "     TaskA counter %d\n", (unsigned)dbg_counterA
  printf "     TaskB counter %d\n", (unsigned)dbg_counterB
  printf " >>\n"
  continue
end

continue
