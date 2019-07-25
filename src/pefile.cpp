#include <QDebug>

#include <fstream>

#include "pefile.h"

PeFile::PeFile(QObject *parent) : QObject(parent)
{

}

PeFile::~PeFile()
{

}

void PeFile::addFunction(const QString &libraryName, const QString &functionName)
{
    // Add a new import function.
    imported_function function;
    function.set_name(functionName.toStdString());

    // Create a new library from which we will import functions.
    import_library *importLibrary = new import_library();

    if (!functions.contains(libraryName)) {
        importLibrary->set_name(libraryName.toStdString());
        functions.insert(libraryName, importLibrary);
    } else {
        importLibrary = functions.value(libraryName);
    }

    // Add imported functions to library.
    importLibrary->add_import(function);
}

void PeFile::applyFunctions(imported_functions_list &imports)
{
    // Add all functions from map to imports list.
    for (import_library *importLibrary : functions.values()) {
        imports.push_back(*importLibrary);
    }

    // Clear read imports from map.
    functions.clear();
}

void PeFile::patchCode(const QString &target, const pe_base &image)
{
    unsigned int baseImageAddress = image.get_image_base_32() + image.get_base_of_code();  // 0x10000000 + 1000 = image.get_image_base_32() + (image.get_base_of_code() or section.get_virtual_address())?

    for (section &section : section_list(image.get_image_sections())) {
        if (section.get_name() == ".text") {
            // Read raw data of section as byte array.
            unsigned char *data = reinterpret_cast<unsigned char*>(const_cast<char*>(section.get_raw_data().c_str()));
            //unsigned char *data = (unsigned char*) section.get_raw_data().c_str();

            qDebug() << "Address of base image:" << showbase << hex << baseImageAddress;

            FunctionMap functionMap = Constants::targets.value(target);

            for (QString &functionName : functionMap.keys()) {
                unsigned int oldAddress = functionMap.value(functionName);
                unsigned int newAddress = addressOfFunctions.value(functionName);

                // TODO: Fix integer vs. char precision lost?...

                // Change old address to point to new function instead.
                data[oldAddress - baseImageAddress] = newAddress; //data[addressOfGetHostByName - addressOfBaseImage];

                qDebug() << showbase << hex << "Patching" << functionName << "changed address" << oldAddress << "to" << newAddress;
            }

            // Write altered raw data of section.
            section.set_raw_data(std::string(reinterpret_cast<char*>(data)));

            // Print out for debugging.
            for (unsigned int i = 0; i < 8; i++) {
                qDebug() << hex << data[i];
            }
        }
    }
}

FunctionMap PeFile::buildAddressOfFunctions(const pe_base &image) {
    FunctionMap map;

    for (import_library library : get_imported_functions(image)) {
        unsigned int address = library.get_rva_to_iat();

        for (imported_function function : library.get_imported_functions()) {
            map.insert(QString::fromStdString(function.get_name()), address);
            address += 4; // Offset is 4 between entries.
        }
    }

    return map;
}

bool PeFile::apply(const QString &path, const QString &target)
{
    QString fileName = path + target + "_patched";

    // Open the file.
    std::ifstream inputStream(fileName.toStdString(), std::ios::in | std::ios::binary);

    if (!inputStream) {
        qDebug() << "Cannot open" << fileName;

        return false;
    }

    try {
        // Create an instance of a PE or PE + class using a factory
        pe_base image(pe_factory::create_pe(inputStream));

        // Get the list of imported libraries and functions.
        imported_functions_list imports = get_imported_functions(image);
        applyFunctions(imports);

        // But we'll just rebuild the import table
        // It will be larger than before our editing
        // so we write it in a new section so that everything fits
        // (we cannot expand existing sections, unless the section is right at the end of the file)
        section importSection;
        importSection.get_raw_data().resize(1);	// We cannot add empty sections, so let it be the initial data size 1
        importSection.set_name(Constants::patch_name.toStdString()); // Section Name
        importSection.readable(true).writeable(true); // Available for read and write

        // Add a section and get a link to the added section with calculated dimensions
        section &attachedSection = image.add_section(importSection);

        // Structure responsible for import reassembler settings
        import_rebuilder_settings settings(true, false); // Modify the PE header and do not clear the IMAGE_DIRECTORY_ENTRY_IAT field
        rebuild_imports(image, imports, attachedSection, settings); // Rebuild Imports

        // Build address to function table.
        addressOfFunctions = buildAddressOfFunctions(image);

        // Patch code.
        patchCode(target, image);

        // Create a new PE file.
        std::ofstream outputStream(fileName.toStdString(), std::ios::out | std::ios::binary | std::ios::trunc);

        if (!outputStream) {
            qDebug() << "Cannot create" << fileName;

            return false;
        }

        // Rebuild PE file.
        rebuild_pe(image, outputStream);

        qDebug() << "PE was rebuilt and saved to" << fileName;
    } catch (const pe_exception &exception) {
        // If an error occurred.
        qDebug() << "Error:" << exception.what();

        return false;
    }

    return true;
}
