#include "../dawn/src/middleware/datagen/family4/character/acquisition_feedback.h"
#include <cstring>
#include <cstdio>

int main() {
    using namespace dawn::middleware::datagen::family4::character;
    layout::Object object{};
    object.characterSoid=42; object.nextInventorySerial=100;
    std::array<AcquisitionNotice,32> pending{};
    std::size_t count=19;
    for (std::size_t i=0;i<count;++i) {
        object.inventoryItems[i].instanceSoid=1000+i;
        object.inventoryItems[i].mutationSerial=static_cast<std::int32_t>(60+i);
        pending[i]={42,1000+i};
    }
    const auto original=object.inventoryItems;
    apply_acquisition_feedback(object,std::span(pending).first(16));
    if (object.inventoryChanges.nextSequence!=16 || object.inventoryChanges.writeSlot!=0) return 1;
    std::array<std::byte,sizeof(object)> encoded{};
    std::memcpy(encoded.data(),&object,sizeof(object));
    layout::Object decoded{};
    std::memcpy(&decoded,encoded.data(),encoded.size());
    for (std::size_t i=0;i<16;++i)
        if (decoded.inventoryChanges.records[i].mutationSerial!=original[i].mutationSerial
            || decoded.inventoryChanges.records[i].kind!=1 || decoded.inventoryChanges.records[i].flags!=0
            || decoded.inventoryChanges.records[i].sequence!=i) return 2;
    if (count!=19 || pending[0].instanceSoid!=1000) return 3;
    commit_acquisition_batch(pending,count,16);
    if (count!=3 || pending[0].instanceSoid!=1016) return 4;
    apply_acquisition_feedback(object,std::span(pending).first(count));
    if (object.inventoryChanges.nextSequence!=3
        || object.inventoryChanges.records[0].mutationSerial!=76) return 5;
    commit_acquisition_batch(pending,count,count);
    apply_acquisition_feedback(object,std::span(pending).first(count));
    if (count || object.inventoryChanges.nextSequence || object.inventoryChanges.records[0].kind) return 6;
    const std::array<AcquisitionNotice,2> foreign{{{43,1000},{42,9999}}};
    apply_acquisition_feedback(object,foreign);
    if (object.inventoryChanges.nextSequence) return 7;
    if (object.inventoryItems[0].instanceSoid!=original[0].instanceSoid
        || object.nextInventorySerial!=100) return 8;
    std::puts("Native acquisition feedback batching and retry checks passed.");
    return 0;
}
