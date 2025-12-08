target extended-remote :3333
set confirm off
set pagination off
set mem inaccessible-by-default off
monitor arm semihosting enable
monitor reset halt

break hrt_error
break hrt_port_sp_valid

break hrt__make_ready
break hrt__pick_next_ready

break hrt__save_current_sp
break hrt__load_next_sp_and_set_current

commands 1
  silent
  bt
  p/x (unsigned)g_error
  continue
end

commands 2
  silent
  printf " >> Checking current sp: sp=0x%08lx -- queue count: %d\n", (unsigned)dbg_curr_sp, (unsigned)dbg_tsk_q
  continue
end

commands 3
  silent
  printf " >> Make ready id: %d state: %d -- queue count: %d\n", (unsigned)dbg_make_ready_id, (unsigned)dbg_make_ready_state, (unsigned)dbg_tsk_q
  continue
end

commands 4
  silent
  printf " >> Next ready task picked id: %d -- queue count: %d\n", (unsigned)dbg_pick, (unsigned)dbg_tsk_q
  continue
end

commands 5
  silent
  printf " >> Save current SP for id: %d sp=0x%08lx -- queue count: %d\n", (unsigned)dbg_id_save, (unsigned)dbg_sp_save, (unsigned)dbg_tsk_q
  continue
end

commands 6
  silent
  printf " >> Load next and set as current id: %d sp=0x%08lx -- queue count: %d\n", (unsigned)dbg_id_load, (unsigned)dbg_sp_load, (unsigned)dbg_tsk_q
  continue
end

continue
