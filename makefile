all:
	g++ ./*.cc -Wall -Og -g -I../include -lpthread -L../libmp3lame/.libs/ -lmp3lame -o mp3_enc
