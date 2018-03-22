# Cloning all repos
#git clone https://github.com/lz4/lz4.git lz4
#git clone https://github.com/andikleen/snappy-c.git snappy
#git clone https://github.com/richgel999/miniz.git miniz
#git clone https://github.com/open-mpi/ompi.git mpi

# Copying all files to relevant dirs
CUR_DIR=$PWD

LIBS_TO_ADD="-Wno-error=unused-command-line-argument $CUR_DIR/miniz/miniz.o $CUR_DIR/miniz/miniz_zip.o $CUR_DIR/miniz/miniz_tinfl.o $CUR_DIR/miniz/miniz_tdef.o $CUR_DIR/snappy/libsnappyc.so.1 $CUR_DIR/lz4/lib/liblz4.a "

FLAGS_TO_ADD="-Wno-error=unused-command-line-argument -I $CUR_DIR/miniz -I $CUR_DIR/snappy -I $CUR_DIR/lz4/lib "

# echo "Running autogen for mpi" First check if we need to run autogen
if [ ! -f mpi/configure ]; then
cd mpi && ./autogen.pl && ./configure --prefix=$CUR_DIR/mpiBuild && cd ..
fi

# Copy files to locations
cp compressLib/minizMakefile miniz/Makefile
cp compressLib/send.c mpi/ompi/mpi/c/send.c
cp compressLib/recv.c mpi/ompi/mpi/c/recv.c
cp compressLib/mpi.h mpi/ompi/include/mpi.h


#Clear our old lines if they are there
sed -i "" "s,^CFLAGS = $LIBS_TO_ADD,CFLAGS = ," $CUR_DIR/mpi/test/monitoring/Makefile
sed -i "" "s,^CFLAGS = $FLAGS_TO_ADD,CFLAGS = ," $CUR_DIR/mpi/ompi/mpi/c/profile/Makefile
sed -i "" "s,^CFLAGS = $FLAGS_TO_ADD,CFLAGS = ," $CUR_DIR/mpi/ompi/mpi/c/Makefile
sed -i "" "s,^CFLAGS = $FLAGS_TO_ADD,CFLAGS = ," $CUR_DIR/mpi/ompi/tools/ompi_info/Makefile


sed -i "" "s,^CFLAGS = ,&$LIBS_TO_ADD," $CUR_DIR/mpi/test/monitoring/Makefile
sed -i "" "s,^CFLAGS = ,&$LIBS_TO_ADD," $CUR_DIR/mpi/ompi/tools/ompi_info/Makefile
sed -i "" "s,^CFLAGS = ,&$FLAGS_TO_ADD," $CUR_DIR/mpi/ompi/mpi/c/profile/Makefile
sed -i "" "s,^CFLAGS = ,&$FLAGS_TO_ADD," $CUR_DIR/mpi/ompi/mpi/c/Makefile

# mkdir -p mpiBuild
#
#
# echo "Building LZ4"
# cd lz4 && make && cd ..
#
# echo "Building snappy"
# cd snappy && make && cd ..
#
# echo "Building miniz"
# cd miniz && make && cd ..
#
# echo "Building MPI"
# cd mpi && make && make install

echo "ALL DONE"
echo "You must put these lines in your ~/.bashrc or ~/.bash_profile"
echo "export PATH=$PWD/mpiBuild/bin:\$PATH"
echo "run source ~/.bashrc or source ~/.bash_profile"
