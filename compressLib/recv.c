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
 * Copyright (c) 2015      Research Organization for Information Science
 *                         and Technology (RIST). All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */
 #include <stdio.h>
 #include <stdlib.h>

#include "ompi_config.h"

#include "ompi/mpi/c/bindings.h"
#include "ompi/runtime/params.h"
#include "ompi/communicator/communicator.h"
#include "ompi/errhandler/errhandler.h"
#include "ompi/mca/pml/pml.h"
#include "ompi/memchecker.h"
#include "ompi/request/request.h"

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
#pragma weak MPI_Recv = PMPI_Recv
#pragma weak MPI_Crecv = PMPI_Crecv
#pragma weak MPI_Crecv_Online = PMPI_Crecv_Online
#endif
#define MPI_Recv PMPI_Recv
#define MPI_Crecv PMPI_Crecv
#define MPI_Crecv_Online PMPI_Crecv_Online


#endif

static const char FUNC_NAME[] = "MPI_Recv";


int MPI_Recv(void *buf, int count, MPI_Datatype type, int source,
             int tag, MPI_Comm comm, MPI_Status *status)
{
    int rc = MPI_SUCCESS;

    MEMCHECKER(
        memchecker_datatype(type);
        memchecker_call(&opal_memchecker_base_isaddressable, buf, count, type);
        memchecker_comm(comm);
    );

    if ( MPI_PARAM_CHECK ) {
        OMPI_ERR_INIT_FINALIZE(FUNC_NAME);
        OMPI_CHECK_DATATYPE_FOR_RECV(rc, type, count);
        OMPI_CHECK_USER_BUFFER(rc, buf, type, count);

        if (ompi_comm_invalid(comm)) {
            return OMPI_ERRHANDLER_INVOKE(MPI_COMM_WORLD, MPI_ERR_COMM, FUNC_NAME);
        } else if (((tag < 0) && (tag != MPI_ANY_TAG)) || (tag > mca_pml.pml_max_tag)) {
            rc = MPI_ERR_TAG;
        } else if ((source != MPI_ANY_SOURCE) &&
                   (MPI_PROC_NULL != source) &&
                   ompi_comm_peer_invalid(comm, source)) {
            rc = MPI_ERR_RANK;
        }

        OMPI_ERRHANDLER_CHECK(rc, comm, rc, FUNC_NAME);
    }

    if (MPI_PROC_NULL == source) {
        if (MPI_STATUS_IGNORE != status) {
            *status = ompi_request_empty.req_status;
        }
        return MPI_SUCCESS;
    }

    OPAL_CR_ENTER_LIBRARY();

    rc = MCA_PML_CALL(recv(buf, count, type, source, tag, comm, status));
    OMPI_ERRHANDLER_RETURN(rc, comm, rc, FUNC_NAME);
}

int MPI_Crecv(char *cBuf, long cBufLen, char *buf, long bufLen, int source, MPI_Comm comm) {
  int returnStatus = 0;
  double decompStart, decompEnd, recvStart, recvEnd;
  int packetSize;
  Packet *packet = (Packet *)cBuf;
  int lz4CmpBytes = 0;
  size_t snappyCmpBytes = 0;
  MPI_Status status;
  mz_ulong minizCmpBytes = 0;
  recvStart = MPI_Wtime();


  MPI_Probe(source, 0, MPI_COMM_WORLD, &status);
  MPI_Get_count(&status, MPI_CHAR, &packetSize);

  returnStatus = MPI_Recv(cBuf, packetSize, MPI_CHAR, source, 0, comm, &status);
  recvEnd = MPI_Wtime();

  decompStart = MPI_Wtime();

  if (packet->compAlg == LZ4) {  // lz4
    lz4CmpBytes = LZ4_decompress_safe(packet->cmpBuf, buf, packet->cmpBytes, packet->inpBytes);
  }
  else if (packet->compAlg == SNAPPY) {  // snappy
    if (!snappy_uncompressed_length (packet->cmpBuf, packet->cmpBytes, &snappyCmpBytes)) {
      printf("Error getting uncompressed length");
      exit(2);
    }
    if (snappy_uncompress(packet->cmpBuf, packet->cmpBytes, buf) < 0) {
      printf("Error in snappy compression\n");\
      exit(2);
    }
  }
  else if (packet->compAlg == MINIZ) {  // miniz
    minizCmpBytes = (mz_ulong)packet->inpBytes;
    if (mz_uncompress((unsigned char *)buf, &minizCmpBytes, (unsigned char *)packet->cmpBuf, packet->cmpBytes) != MZ_OK) {
      printf("Error in miniz compression\n");
      exit(3);
    }
  }
  else {
    printf("Error: unknown compress_alg flag\n");
    exit(packet->compAlg);
  }

  decompEnd = MPI_Wtime();

  if (lz4CmpBytes < 1 && snappyCmpBytes < 1 && minizCmpBytes < 1) {
    printf("Error decompressing\n");
    exit(-1);
  }
  printf("Decompression Stats\n");
  printf("\tNumber of receives: 2\n");
  printf("\tDecompress Time: %f\n\tRecv Time: %f\n\tSum of Time: %f\n", decompEnd - decompStart, recvEnd - recvStart, decompEnd - decompStart + recvEnd - recvStart);

  return returnStatus;

}

int MPI_Crecv_Online(char *buf, long bufLen, int source, MPI_Comm comm) {

  LZ4_streamDecode_t* const lz4Stream = LZ4_createStreamDecode();
  static Packet packet;
  size_t snappyCmpBytes;
  int returnStatus = 0, cmpBytes;
  long count, total = 0, numProbes = 0, numRecvs = 1;
  int packetSize;
  double probeStart, probeEnd, probeTotal = 0, countStart, countEnd, countTotal = 0;
  MPI_Status status;

  double recvStart, recvEnd, recvTotal = 0, decompStart, decompEnd, decompTotal = 0;
  mz_ulong uncompressSize;
  recvStart = MPI_Wtime();
  MPI_Recv(&count, 1, MPI_LONG, source, 0, comm, &status);
  recvEnd = MPI_Wtime();
  recvTotal += recvEnd - recvStart;

  while (total != count) {

    // Probeing and timing
    probeStart = MPI_Wtime();
    MPI_Probe(source, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
    probeEnd = MPI_Wtime();
    probeTotal += probeEnd - probeStart;

    // Get Count and Timing
    countStart = MPI_Wtime();
    MPI_Get_count(&status, MPI_CHAR, &packetSize);
    countEnd = MPI_Wtime();
    countTotal += countEnd - countStart;

    // Recv and timing
    recvStart = MPI_Wtime();
    returnStatus = returnStatus || MPI_Recv(&packet, packetSize, MPI_CHAR, source, status.MPI_TAG, comm, &status);
    recvEnd = MPI_Wtime();
    recvTotal += recvEnd - recvStart;

    decompStart = MPI_Wtime();

    if (packet.compAlg == LZ4) {  // lz4
      cmpBytes = LZ4_decompress_safe_continue(lz4Stream, packet.cmpBuf, buf + (status.MPI_TAG * MAX_MESSAGE_BYTES), packet.cmpBytes, packet.inpBytes);
    }
    else if (packet.compAlg == SNAPPY) {  // snappy
      if (!snappy_uncompressed_length (packet.cmpBuf, packet.cmpBytes, &snappyCmpBytes)) {
        printf("Error getting uncompressed length");
        exit(2);
      }
      cmpBytes = (int)snappyCmpBytes;
      if (snappy_uncompress(packet.cmpBuf, packet.cmpBytes, buf + (status.MPI_TAG * MAX_MESSAGE_BYTES)) < 0) {
        printf("Error in snappy compression\n");\
        exit(2);
      }
    }
    else if (packet.compAlg == MINIZ) {  // miniz
      uncompressSize = (mz_ulong)packet.inpBytes;
      if (mz_uncompress((unsigned char *)(buf + (status.MPI_TAG * MAX_MESSAGE_BYTES)), &uncompressSize,(unsigned char *)packet.cmpBuf, packet.cmpBytes) != MZ_OK) {
        printf("Error in miniz compression\n");
        exit(3);
      }
      cmpBytes = (int)uncompressSize;

    }
    else {
      printf("Error: unknown compress_alg flag\n");
      exit(packet.compAlg);
    }
    decompEnd = MPI_Wtime();
    decompTotal += decompEnd - decompStart;
    numProbes ++;
    numRecvs++;
    if (cmpBytes != packet.inpBytes) {
      // printf("Error!!!\n");
    }
    total += packet.inpBytes;
  }
  free(lz4Stream);
  printf("Decompression Stats\n");
  printf("\tNumber of receives: %ld\n\tNumber of Probes and getCounts: %ld\n", numRecvs, numProbes);
  printf("\tDecompress Time: %f\n\tRecv Time: %f\n\tProbe Time: %f\n\tGet Count Time: %f\n\tSum of Time: %f\n", decompTotal, recvTotal, probeTotal, countTotal, decompTotal + recvTotal + probeTotal + countTotal);
  return returnStatus;
}
