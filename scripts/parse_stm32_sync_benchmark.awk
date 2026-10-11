# SPDX-License-Identifier: Apache-2.0
# Validate the complete 14-metric GDB result and produce a canonical CSV.
# Call: awk -v target=N -v csv=output.csv -f scripts/parse_stm32_sync_benchmark.awk gdb.log

BEGIN {
  split("id bytes count api_min api_avg api_max masked_min masked_avg masked_max", keys, " ")
  split("0 0 0 0 4 4 16 16 64 64 256 256 1024 1024", sizes, " ")
  print "id,bytes,count,api_min,api_avg,api_max,masked_min,masked_avg,masked_max" > csv
}
/^core_hz=/ {
  if (NF != 5) bad=1
  split($1, hz, "="); split($2, n, "="); split($3, rc, "=")
  split($4, prof, "="); split($5, err, "=")
  if (hz[1]!="core_hz" || hz[2]+0<=0 || n[1]!="samples" || n[2]+0!=target ||
      rc[1]!="rows" || rc[2]+0!=14 || prof[1]!="profile" || prof[2]+0!=1 ||
      err[1]!="error" || err[2]+0!=0) bad=1
  metadata++
}
/^BENCH / {
  if (NF != 10) { bad=1; next }
  delete v
  for (k=1;k<=9;k++) {
    if (split($(k+1), p, "=")!=2 || p[1]!=keys[k] || p[2] !~ /^[0-9]+$/) bad=1
    v[keys[k]]=p[2]
  }
  if (bad) next
  if (v["id"]+0!=rows || rows>=14 || v["bytes"]+0!=sizes[rows+1] ||
      v["count"]+0!=target ||
      v["api_min"]+0>v["api_avg"]+0 ||
      v["api_avg"]+0>v["api_max"]+0 ||
      v["masked_min"]+0>v["masked_avg"]+0 ||
      v["masked_avg"]+0>v["masked_max"]+0 ||
      v["masked_min"]+0>v["api_max"]+0) bad=1
  printf "%s,%s,%s,%s,%s,%s,%s,%s,%s\n",
    v["id"],v["bytes"],v["count"],v["api_min"],v["api_avg"],v["api_max"],
    v["masked_min"],v["masked_avg"],v["masked_max"] >> csv
  rows++
}
/^RESULT: PASS$/ { pass++ }
END {
  close(csv)
  if (bad || metadata!=1 || rows!=14 || pass!=1) {
    printf "Synchronization DWT record rejected: metadata=%d rows=%d pass=%d bad=%d\n",
      metadata,rows,pass,bad > "/dev/stderr"
    exit 1
  }
}

