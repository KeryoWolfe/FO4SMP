#include "hdtDefaultBBPList.h"

#include "XmlReader.h"

namespace hdt
{
	// Moved unchanged from DefaultBBP::loadDefaultBBPs so Fallout 4 can use it.
	DefaultBBPList parseDefaultBBPs(std::string_view xml)
	{
		DefaultBBPList list;
		if (xml.empty())
			return list;

		std::string buffer(xml);
		XMLReader reader(reinterpret_cast<uint8_t*>(buffer.data()), buffer.size());

		reader.nextStartElement();
		if (reader.GetName() != "default-bbps")
			return list;
		list.valid = true;

		while (reader.Inspect()) {
			if (reader.GetInspected() == Xml::Inspected::StartTag) {
				if (reader.GetName() == "map") {
					try {
						auto shape = reader.getAttribute("shape");
						auto file = reader.getAttribute("file");
						list.files.insert(std::make_pair(shape, file));
					} catch (...) {
						logger::warn("defaultBBP({},{}) : invalid map", reader.GetRow(), reader.GetColumn());
					}
					reader.skipCurrentElement();
				} else if (reader.GetName() == "remap") {
					auto target = reader.getAttribute("target");
					DefaultBBPList::Remap remap = { target, {}, {} };
					while (reader.Inspect()) {
						if (reader.GetInspected() == Xml::Inspected::StartTag) {
							if (reader.GetName() == "source") {
								int priority = 0;
								try {
									priority = reader.getAttributeAsInt("priority");
								} catch (...) {}
								auto source = reader.readText();
								remap.entries.insert({ priority, source });
							} else if (reader.GetName() == "requires") {
								auto req = reader.readText();
								remap.required.insert(req);
							} else {
								logger::warn("defaultBBP({},{}) : unknown element", reader.GetRow(), reader.GetColumn());
								reader.skipCurrentElement();
							}
						} else if (reader.GetInspected() == Xml::Inspected::EndTag) {
							break;
						}
					}
					list.remaps.push_back(remap);
				} else {
					logger::warn("defaultBBP({},{}) : unknown element", reader.GetRow(), reader.GetColumn());
					reader.skipCurrentElement();
				}
			} else if (reader.GetInspected() == Xml::Inspected::EndTag) {
				break;
			}
		}

		return list;
	}
}
