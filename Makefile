# A simple makefile for unify and unipatch

CC= cc
CFLAGS= -O

all: unify unipatch

unify: unify.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o unify unify.c

unipatch: unipatch.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o unipatch unipatch.c
