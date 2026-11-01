#include "JuceBlocks.h"

#include "Amp/NamAmpBlock.h"
#include "Cab/IrLoaderBlock.h"
#include "../Graph/BlockFactory.h"

namespace kumamp
{
void registerJuceBlocks()
{
    registerStoreCreator (BlockType::NamAmp,   [] (StoreId id) -> std::shared_ptr<ParamStore> { return std::make_shared<NamAmpBlock> (id); });
    registerStoreCreator (BlockType::IrLoader, [] (StoreId id) -> std::shared_ptr<ParamStore> { return std::make_shared<IrLoaderBlock> (id); });
}
} // namespace kumamp
