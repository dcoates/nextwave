#include <arrayfire.h>

#include <stdio.h>

#include <fstream>
#include <iostream>

//#include <crtdbg.h>

using namespace std;

#include <json.hpp>
using json=nlohmann::json;

#include "../include/spdlog/spdlog.h"

// For NextWave Plugin
#include "nextwave_plugin.hpp"
#pragma pack(push,1)
#include "memory_layout.h"
#include "layout_boxes.h"
#pragma pack(pop) // restore previous setting

#if _WIN64
#include <windows.h>
#include <strsafe.h>
#include "boost/interprocess/windows_shared_memory.hpp"
#else
#include <boost/interprocess/shared_memory_object.hpp>
// On unix, for dl* functions:
#include <dlfcn.h>
#endif
#include <boost/interprocess/mapped_region.hpp>

#define VERBOSE 0

// Globals
#define BUF_SIZE (2048*2048)

unsigned char buffer[BUF_SIZE];
unsigned int nCurrRing=0;
//LARGE_INTEGER t2, t1;

uint64_t xcoord[BUF_SIZE];
uint64_t ycoord[BUF_SIZE];

// https://stackoverflow.com/questions/8583308/timespec-equivalent-for-windows
inline double time_highres() {
#if _WIN64
  LARGE_INTEGER freq, now;
	QueryPerformanceCounter(&now);
	return now.QuadPart / freq.QuadPart * 1e6;
#else
  return 0;
#endif
}

class af_instance {
public: 

  af_instance() {
  };

  ~af_instance() {
  };

  af::array im_xcoor;
  af::array im_ycoor;

  af::array box_x;
  af::array box_y;

  af::array im;
  af::array cen_x;
  af::array cen_y;

  af::array weighted_x;
  af::array weighted_y;

  af::array sums_x;
  af::array sums_y;

  af::array seq1;

  af::dim4 new_dims;//(BOX_SIZE*BOX_SIZE,NBOXES); // AF stacks the boxes, so do this to reshape for easier sum reduction
};

class af_instance *gaf;

//#define WIDTH 2048
//#define HEIGHT 2048
#define BOX_SIZE 40 // TODO
#define NBOXES 437 // TODO
float fbuffer[BUF_SIZE];
int nbuffer[BUF_SIZE];

uint16_t num_boxes;
double box_size;
double pupil_radius_pixels;


using namespace boost::interprocess;

#if _WIN64
windows_shared_memory shmem1;// (open_or_create, SHMEM_HEADER_NAME, read_write, (size_t)SHMEM_HEADER_SIZE);
windows_shared_memory shmem2;// (open_or_create, SHMEM_BUFFER_NAME, read_write, (size_t)SHMEM_BUFFER_SIZE);
windows_shared_memory shmem3;// (open_or_create, SHMEM_BUFFER_NAME2, read_write, (size_t)SHMEM_BUFFER_SIZE2);
#else
shared_memory_object shmem1;// (open_or_create, SHMEM_HEADER_NAME, read_write);
shared_memory_object shmem2;// (open_or_create, SHMEM_BUFFER_NAME, read_write);
shared_memory_object shmem3;// (open_or_create, SHMEM_BUFFER_NAME2, read_write);
#endif

// Common to both OSes:
mapped_region shmem_region1; //{ shmem, read_write };
mapped_region shmem_region2; //{ shmem2, read_write };
mapped_region shmem_region3; //{ shmem3, read_write };

void read_boxes(int width) {
  // TODO: is dynamically changing the size allowed?
  struct shmem_boxes_header* pShmemBoxes = (struct shmem_boxes_header*) shmem_region3.get_address();
  num_boxes = pShmemBoxes->num_boxes;
  box_size = pShmemBoxes->box_size;
  int box_size_int = pShmemBoxes->box_size;
  pupil_radius_pixels = pShmemBoxes->pupil_radius_pixels;

  float x_ref0 = pShmemBoxes->reference_x[0];
  float y_ref0 = pShmemBoxes->reference_y[0];

	gaf->box_x = af::array(box_size,num_boxes,pShmemBoxes->reference_x);
	gaf->box_y = af::array(box_size,num_boxes,pShmemBoxes->reference_y);

  // Each box will have a set of 1D indices into its members
	for (int ibox=0; ibox<num_boxes; ibox++) {
		int ibox_x = pShmemBoxes->reference_x[ibox]-box_size/2;
		int ibox_y = pShmemBoxes->reference_y[ibox]-box_size/2;
		for (int y=0; y<box_size; y++)  {
			for (int x=0; x<box_size; x++) {
        int posx=ibox_x+x;
        int posy=ibox_y+y;
        int idx_1d=posx*width+posy; // Column major // TODO: height
        //if (ibox==0) {
        //printf("%d ",idx_1d);
        //}
				//nbuffer[ibox*box_size_int*box_size_int+x*box_size_int+y]=idx_1d;
				nbuffer[ibox*box_size_int*box_size_int+x*box_size_int+y]=idx_1d;
			}
		}
	}
	gaf->seq1 = af::array(box_size*box_size, num_boxes, nbuffer );

  spdlog::info("boxes: #={} size={} pupil={}", num_boxes, box_size, pupil_radius_pixels );
  spdlog::info("x0={} y0={}", x_ref0, y_ref0 );

#if VERBOSE
  printf("%f\n",(float)af::max<float>(gaf->seq1) );
  printf("mn1: %f\n",(float)af::min<float>(gaf->seq1(af::span,1)) );
  printf("mn2: %f\n",(float)af::min<float>(gaf->seq1(af::span,2)) );
  printf("mx1: %f\n",(float)af::max<float>(gaf->seq1(af::span,1)) );
  printf("mx2: %f\n",(float)af::max<float>(gaf->seq1(af::span,2)) );
  printf("count1: %f\n",(float)af::count<float>(gaf->seq1(af::span,1)) );
#endif
}

#if 0
void read_boxes_old() {
  int width=1000;
	int width=width;

  // x locations of boxes
	for (int ibox=0; ibox<nboxes; ibox++) {
		for (int y=0; y<box_size; y++) {
			nbuffer[ibox*box_size+y]=ibox*box_size+y; //0+940-80*5;
		}
	}
	gaf->box_x = af::array(box_size,nboxes,nbuffer);

	for (int ibox=0; ibox<nboxes; ibox++) {
		for (int y=0; y<box_size; y++) {
			nbuffer[ibox*box_size+y]=ibox*box_size+y; //+940-80*4;
		}
	}
	gaf->box_y = af::array(box_size,nboxes,nbuffer);

  // each box will have a set of 1d indices into its members
	for (int ibox=0; ibox<nboxes; ibox++) {
		int ibox_x = ibox % 20;
		int ibox_y = int(float(ibox) / 20);
		for (int y=0; y<box_size; y++)  {
			for (int x=0; x<box_size; x++) {
				int posx=ibox_x*box_size+x+25 + rand() % 20;        ; // +25 just to approx. center spots in test img. todo!!
				int posy=ibox_y*box_size+y+25 + rand() % 20;
				nbuffer[ibox*box_size*box_size+y*box_size+x]=posy*width+posx; //+940-80*4;
			}
		}
	}
	box_idx1 = af::array(box_size*box_size, nboxes, nbuffer );

  seqx=(box_x);
  seqy=(box_y);
  gaf->seq1=box_idx1;

  return;
}
#endif //0


DECL init(char *params)
{
  try {
    af::setBackend(AF_BACKEND_CPU);
    spdlog::info("Set AF_BACKEND_CPU");
  } catch (...){
    spdlog::error("Couldn't load AF BACKEND");
  }

#if _WIN64
  shmem1=windows_shared_memory(open_or_create, SHMEM_HEADER_NAME, read_write, (size_t)SHMEM_HEADER_SIZE);
  shmem2=windows_shared_memory(open_or_create, SHMEM_BUFFER_NAME, read_write, (size_t)SHMEM_BUFFER_SIZE);
  shmem3=windows_shared_memory(open_or_create, SHMEM_BUFFER_NAME2, read_write, (size_t)SHMEM_BUFFER_SIZE2);
#else
  shmem1=shared_memory_object(open_or_create, SHMEM_HEADER_NAME, read_write);
  shmem2=shared_memory_object(open_or_create, SHMEM_BUFFER_NAME, read_write);
  shmem3=shared_memory_object(open_or_create, SHMEM_BUFFER_NAME2, read_write);
#endif

  shmem_region1=mapped_region(shmem1, read_write);
  shmem_region2=mapped_region(shmem2, read_write);
  shmem_region3=mapped_region(shmem3, read_write);
  shmem1.truncate((size_t)SHMEM_HEADER_SIZE);
  shmem2.truncate((size_t)SHMEM_BUFFER_SIZE);
  shmem3.truncate((size_t)SHMEM_BUFFER_SIZE2);

  gaf=new af_instance();

  int width=1000; // TODO: fixme
  int height=1000;

  // Precompute indexing arrays for weighted average
	int WIDTH=width;
	int HEIGHT = height;
	for (int x=0; x<WIDTH; x++) {
		for (int y=0; y<HEIGHT; y++) {
			fbuffer[y*WIDTH+x]=(float)x;
		}
	}
	gaf->im_xcoor=af::array(WIDTH, HEIGHT, fbuffer); // wedth and hight sietched? TODO

	for (int x=0; x<WIDTH; x++) {
		for (int y=0; y<HEIGHT; y++) {
			fbuffer[y*WIDTH+x]=(float)y;
		}
	}
	gaf->im_ycoor=af::array(WIDTH, HEIGHT, fbuffer);

  gaf->new_dims=af::dim4(BOX_SIZE*BOX_SIZE,NBOXES); // AF stacks the boxes, so do this to reshape for easier sum reduction

  //read_boxes();

  return 0;
}
// https://stackoverflow.com/questions/49692531/transfer-data-from-arrayfire-arrays-to-armadillo-structures
// https://github.com/arrayfire/arrayfire/issues/1405;
int find_cendroids_af(unsigned char *buffer, int width, int height) {

	gaf->im=af::array(width, height, buffer).as(f32);

	gaf->im /= 255.0;

#if VERBOSE
  spdlog::info("mean={} min={} max={}",(float)af::mean<float>(im),
         (float)af::min<float>(im),
               (float)af::max<float>(im) );
#endif

	gaf->weighted_x = gaf->im * gaf->im_xcoor;
	gaf->weighted_y = gaf->im * gaf->im_ycoor;


#if VERBOSE
  spdlog::info("mean={} min={} max={}",(float)af::mean<float>(weighted_x),
               (float)af::min<float>(weighted_x),
               (float)af::max<float>(weighted_x) );
#endif

	int doprint=0;
	if (doprint)
		af_print(gaf->seq1);

	af::array atemp = gaf->weighted_x(gaf->seq1); //weighted_x or im_xcoor for debug

#if VERBOSE
  spdlog::info("mean={} min={} max={}",(float)af::mean<float>(atemp),
               (float)af::min<float>(atemp),
               (float)af::max<float>(atemp) );
#endif

	if (doprint*0)
		af_print(atemp );
	af::array x_reshape = moddims( atemp, gaf->new_dims );

	if (doprint*0)
		af_print(x_reshape );

	atemp = gaf->weighted_y(gaf->seq1);
	af::array y_reshape = af::moddims( atemp, gaf->new_dims );
	if (doprint*0)
		af_print(y_reshape );

	af::array im2 = gaf->im(gaf->seq1);
	af::array im_reshape = af::moddims( im2, gaf->new_dims );

	af::array sums = af::sum( im_reshape, 0);
	gaf->sums_x = af::sum( x_reshape, 0) / sums;
	gaf->sums_y = af::sum( y_reshape, 0) / sums;

  float *host_x = gaf->sums_x.host<float>();
  float *host_y = gaf->sums_y.host<float>();

  struct shmem_boxes_header* pShmemBoxes = (struct shmem_boxes_header*) shmem_region3.get_address();
  memcpy(pShmemBoxes->centroid_x, host_x, sizeof(float)*num_boxes);
  memcpy(pShmemBoxes->centroid_y, host_y, sizeof(float)*num_boxes);

#if VERBOSE
  spdlog::info("Val0 x,y: {},{}",host_x[0],host_y[0]);
	spdlog::info("Count: {}", (float)af::count<float>(sums_x));
#endif

  af::freeHost(host_x);
  af::freeHost(host_y);

	if (0) {
		af_print( gaf->sums_x );
		af_print( gaf->sums_y );
	}

#if VERBOSE
	spdlog::info("Minsum: {}", (float)af::min<float>(sums));
	spdlog::info("Maxsum: {}", (float)af::max<float>(sums));
	spdlog::info("Maxx: {}", (float)af::max<float>(sums_x));
	spdlog::info("Maxy: {}", (float)af::max<float>(sums_y));
#endif

  return 0;
}

DECL process(char *params)
{
  // TODO: is dynamically changing the size allowed?
	struct shmem_header* pShmem = (struct shmem_header*) shmem_region1.get_address();
	uint16_t nCurrRing = pShmem->current_frame;
	uint16_t height = pShmem->dimensions[0];
	uint16_t width = pShmem->dimensions[1];

	memcpy((void*)buffer,
         ((const char *)(shmem_region2.get_address()))+height*width*nCurrRing, height*width);

#if VERBOSE
	spdlog::info("Dims: {} {} {}", width, height, int(buffer[0])) ;
#endif

  // If we need to...
  read_boxes(width);

  find_cendroids_af(buffer, width, height);
	return 0;
};

DECL plugin_close(char *params)
{
  std::cout <<"CEN close\n" ;
  return 0;
};

DECL set_params(char* settings_as_json)
{
  std::cout <<"P1 set_params " << settings_as_json << "\n";
  return 0;
};
 
DECL get_info(char* which_info)
{
  std::cout <<"P1 get_info";
  return 0;
};



