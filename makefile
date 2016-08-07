RTSS_Timer.exe: RTSS_Timer.cpp
	cl /O2 /GS- RTSS_Timer.cpp kernel32.lib user32.lib shell32.lib winmm.lib
