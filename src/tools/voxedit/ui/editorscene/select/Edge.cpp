#include "Edge.h"

namespace voxedit {
namespace selections {

bool Edge::execute(voxel::RawVolume::Sampler& model, voxel::RawVolume::Sampler& selection) const {
	return true;
}

}
}