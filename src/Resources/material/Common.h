#ifndef MATERIAL_COMMON_H
#define MATERIAL_COMMON_H

#include <boost/array.hpp>


namespace material {


/// The types of the rendering passes
enum PassType
{
	COLOR_PASS,
	DEPTH_PASS,
	PASS_TYPES_NUM
};


extern boost::array<const char*, PASS_TYPES_NUM> passNames;


} // end namespace


#endif