/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2004-2007 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2005 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2008 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2013      Los Alamos National Security, LLC.  All rights
 *                         reserved.
 * Copyright (c) 2015      Research Organization for Information Science
 *                         and Technology (RIST). All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */
#include "ompi_config.h"
#include <stdio.h>
#include <stdlib.h>

#include "ompi/mpi/c/bindings.h"
#include "ompi/runtime/params.h"
#include "ompi/communicator/communicator.h"
#include "ompi/errhandler/errhandler.h"
#include "ompi/mca/pml/pml.h"
#include "ompi/datatype/ompi_datatype.h"
#include "ompi/memchecker.h"
#include <miniz.h>
#include <snappy.h>
#include <lz4.h>

#define MAX_MESSAGE_BYTES 307200
#define MAX_CMP_BUF_BYTES LZ4_COMPRESSBOUND(MAX_MESSAGE_BYTES) + MAX_MESSAGE_BYTES

typedef struct {
  unsigned int inpBytes;
  unsigned int cmpBytes;
  unsigned char compAlg;
  char cmpBuf[MAX_CMP_BUF_BYTES];
} Packet;


#define RAW_PACKET_SIZE sizeof(unsigned int) * 2 + sizeof(char)
#define PACKET_SIZE sizeof(Packet)

#if OMPI_BUILD_MPI_PROFILING
#if OPAL_HAVE_WEAK_SYMBOLS

#pragma weak MPI_Send = PMPI_Send
#pragma weak MPI_Csend = PMPI_Csend
#pragma weak MPI_Csend_Online = PMPI_Csend_Online
#endif
#define MPI_Send PMPI_Send
#define MPI_Csend PMPI_Csend
#define MPI_Csend_Online PMPI_Csend_Online

#endif

static const char FUNC_NAME[] = "MPI_Send";


int MPI_Send(const void *buf, int count, MPI_Datatype type, int dest,
             int tag, MPI_Comm comm)
{
    int rc = MPI_SUCCESS;

    MEMCHECKER(
        memchecker_datatype(type);
        memchecker_call(&opal_memchecker_base_isdefined, buf, count, type);
        memchecker_comm(comm);
    );

    if ( MPI_PARAM_CHECK ) {
        OMPI_ERR_INIT_FINALIZE(FUNC_NAME);
        if (ompi_comm_invalid(comm)) {
            return OMPI_ERRHANDLER_INVOKE(MPI_COMM_WORLD, MPI_ERR_COMM, FUNC_NAME);
        } else if (count < 0) {
            rc = MPI_ERR_COUNT;
        } else if (tag < 0 || tag > mca_pml.pml_max_tag) {
            rc = MPI_ERR_TAG;
        } else if (ompi_comm_peer_invalid(comm, dest) &&
                   (MPI_PROC_NULL != dest)) {
            rc = MPI_ERR_RANK;
        } else {
            OMPI_CHECK_DATATYPE_FOR_SEND(rc, type, count);
            OMPI_CHECK_USER_BUFFER(rc, buf, type, count);
        }
        OMPI_ERRHANDLER_CHECK(rc, comm, rc, FUNC_NAME);
    }

    if (MPI_PROC_NULL == dest) {
        return MPI_SUCCESS;
    }

    OPAL_CR_ENTER_LIBRARY();
    rc = MCA_PML_CALL(send(buf, count, type, dest, tag, MCA_PML_BASE_SEND_STANDARD, comm));
    OMPI_ERRHANDLER_RETURN(rc, comm, rc, FUNC_NAME);
}

int MPI_Csend(char *cBuf, long cBufLen, char *buf, long bufLen, int *dest, int numDest,  MPI_Comm comm,  const unsigned char compressAlg) {

  int returnStatus, i;
  Packet *packet = (Packet *)cBuf;
  double compStart, compEnd, sendStart, sendEnd;

  mz_ulong compressSize = (mz_ulong)cBufLen;

  size_t snappyCompSize;
  static int snappy_init = 0;
  static struct snappy_env env;
  if (!snappy_init) {
    snappy_init_env(&env);
    snappy_init = 1;
  }
  packet->inpBytes = bufLen;
  packet->compAlg = compressAlg;

  compStart = MPI_Wtime();
  if (compressAlg == LZ4) {  // lz4
    packet->cmpBytes = LZ4_compress_fast(buf, packet->cmpBuf, bufLen, cBufLen - RAW_PACKET_SIZE, 1);
  }
  else if (compressAlg == SNAPPY) {  // snappy
    snappyCompSize = (size_t)cBufLen - RAW_PACKET_SIZE;
    if (snappy_compress(&env, buf, bufLen, packet->cmpBuf, &snappyCompSize) < 0) {
      printf("Error in snappy compression\n");\
      exit(2);
    }
    packet->cmpBytes = (int)snappyCompSize;
  }
  else if (compressAlg == MINIZ) {  // miniz

    if (mz_compress((unsigned char *)packet->cmpBuf, &compressSize, (unsigned char *)buf, bufLen) != MZ_OK) {
      printf("Error in miniz compression\n");
      exit(3);
    }
    packet->cmpBytes = (int)compressSize;
  }
  else {
    printf("Error: unknown compress_alg flag\n");
    exit(packet->compAlg);
  }
  compEnd = MPI_Wtime();

  if (packet->cmpBytes < 1) {
    printf("Error compressing\n");
  }

  sendStart = MPI_Wtime();
  for (i = 0; i < numDest; i++) {
    returnStatus = MPI_Send(packet, RAW_PACKET_SIZE + packet->cmpBytes, MPI_CHAR, dest[i], 0, comm);
  }
  sendEnd = MPI_Wtime();
  printf("Compression Stats\n");
  printf("\tOriginal Data Size: %ld\n\tCompressed Data Size: %d\n\tCompression Ratio: %f\n", bufLen, packet->cmpBytes, ((float)bufLen) / packet->cmpBytes);
  printf("\tNumber of Sends: 2\n");
  printf("\tCompress Time: %f\n\tSend Time: %f\n\tSum of Time: %f\n", compEnd - compStart, sendEnd - sendStart, sendEnd - sendStart + compEnd - compStart);
  return returnStatus;
}


int MPI_Csend_Online(char *cBuf, long cBufLen, char *buf, long bufLen, int *dest, int numDest,  MPI_Comm comm,  const unsigned char compressAlg) {
  LZ4_stream_t* const lz4Stream = LZ4_createStream();
  Packet *packet = (Packet *)cBuf;
  int returnStatus = 0, i;
  long origSize = bufLen;
  long counter = 0, numSends = 1, compBytes = 0;
  double compStart, compEnd, compTotal = 0, sendStart, sendEnd, sendTotal = 0;
  static int snappy_init = 0;
  static struct snappy_env env;
  MPI_Request request;
  if (!snappy_init) {
    snappy_init_env(&env);
    snappy_init = 1;
  }

  mz_ulong compressSize = MAX_CMP_BUF_BYTES;
  size_t snappyCompSize;

  for (i = 0; i < numDest; i++) {
    MPI_Send(&bufLen, 1, MPI_LONG, dest[i], 0, comm);
  }

  while (bufLen > 0) {
    packet = (Packet *)cBuf;
    packet->compAlg = compressAlg;
    if (bufLen >= MAX_MESSAGE_BYTES) {
      packet->inpBytes = MAX_MESSAGE_BYTES;
    }
    else {
      packet->inpBytes = bufLen;
    }
    bufLen -= MAX_MESSAGE_BYTES;

    compStart = MPI_Wtime();
    if (compressAlg == LZ4) {  // lz4
      packet->cmpBytes = LZ4_compress_fast_continue(lz4Stream, buf, packet->cmpBuf, packet->inpBytes, MAX_CMP_BUF_BYTES, 1);
      if (packet->cmpBytes < 1) {
        printf("Error in lz4 compression\n");
        exit(1);
      }
    }
    else if (compressAlg == SNAPPY) {  // snappy
      snappyCompSize = MAX_CMP_BUF_BYTES;

      if (snappy_compress(&env, buf, packet->inpBytes, packet->cmpBuf, &snappyCompSize) < 0) {
        printf("Error in snappy compression\n");\
        exit(2);
      }
      packet->cmpBytes = (int)snappyCompSize;
    }
    else if (compressAlg == MINIZ) {  // miniz
      compressSize = MAX_CMP_BUF_BYTES;
      if (mz_compress((unsigned char *)packet->cmpBuf, &compressSize, (unsigned char *)buf, (mz_ulong)packet->inpBytes) != MZ_OK) {

        printf("Error in miniz compression\n");
        exit(3);
      }
      packet->cmpBytes = (int)compressSize;
    }
    else {
      printf("Error: unknown compress_alg flag\n");
      exit(compressAlg);
    }
    compEnd = MPI_Wtime();
    compBytes += packet->cmpBytes;

    compTotal += compEnd - compStart;
    if (packet->cmpBytes < 1) {
      printf("Error compressing, inpyBytes: %d, cmpBytes: %d\n", packet->inpBytes, packet->cmpBytes);
    }

    sendStart = MPI_Wtime();
    for (i = 0; i < numDest; i++) {
      returnStatus = returnStatus || MPI_Isend(packet, RAW_PACKET_SIZE + packet->cmpBytes, MPI_CHAR, dest[i], counter, comm, &request);
    }

    sendEnd = MPI_Wtime();
    sendTotal += sendEnd - sendStart;

    numSends++;

    counter ++;
    buf += packet->inpBytes;
    cBuf += RAW_PACKET_SIZE + packet->cmpBytes;
  }

  free(lz4Stream);
  printf("Compression Stats\n");
  printf("\tOriginal Data Size: %ld\n\tCompressed Data Size: %ld\n\tCompression Ratio: %f \n", origSize, compBytes, ((float)origSize) / compBytes);
  printf("\tNumber of Sends: %ld\n", numSends);
  printf("\tCompress Time: %f\n\tSend Time: %f\n\tSum of Time: %f\n", compTotal, sendTotal, compTotal + sendTotal);
  return returnStatus;
}
