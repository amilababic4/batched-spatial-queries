if(NOT DEFINED PROGRAM OR NOT DEFINED N OR NOT DEFINED M)
  message(FATAL_ERROR "Nedostaju PROGRAM, N ili M za provjeru psi_seq_opt")
endif()
execute_process(
  COMMAND "${PROGRAM}" --n "${N}" --m "${M}" --w 0.1 --dist uniform --seed 42 --repeats 1
  RESULT_VARIABLE rezultat OUTPUT_VARIABLE izlaz ERROR_VARIABLE greska)
message("${izlaz}")
if(NOT "${rezultat}" STREQUAL "0")
  message(FATAL_ERROR "psi_seq_opt izlaz=${rezultat}: ${greska}")
endif()
string(FIND "${izlaz}" "OK: svih ${M} upita" nadjeno)
if(nadjeno EQUAL -1)
  message(FATAL_ERROR "Nije potvrdeno poklapanje s brute-force referencom: ${greska}")
endif()
