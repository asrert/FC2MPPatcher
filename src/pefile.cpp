#include <QDebug>

#include <fstream>
#include <iostream>

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

    //func.set_iat_va(0x1);	 // Write a non-zero absolute address in the import address table

    // We have specified incorrect contents (0x1 and 0x2) for the cells where the addresses of the imported functions will be written
    // It doesn't matter in the general case, because these values ​​are always overwritten by the loader.
    // These addresses are important only if the exe file has import bound

    // Create a new library from which we will import functions.
    import_library *importLibrary = new import_library();

    if (!functionMap.contains(libraryName)) {
        importLibrary->set_name(libraryName.toStdString());
        functionMap.insert(libraryName, importLibrary);
    } else {
        importLibrary = functionMap.value(libraryName);
    }

    // Add imported functions to library.
    importLibrary->add_import(function);
}

void PeFile::applyFunctions(imported_functions_list &imports)
{
    // Add all functions from map to imports list.
    for (import_library *importLibrary : functionMap.values()) {
        imports.push_back(*importLibrary);
    }

    // Clear read imports from map.
    functionMap.clear();
}

QHash<QString, unsigned int> PeFile::buildAddressOfFunctionMap(const pe_base &image) {
    QHash<QString, unsigned int> map;

    for (import_library library : get_imported_functions(image)) {
        unsigned int address = library.get_rva_to_iat();

        for (imported_function function : library.get_imported_functions()) {
            map.insert(QString::fromStdString(function.get_name()), address);
            address += 4; // Offset is 4 between entries.
        }
    }

    return map;
}

bool PeFile::apply(const QString &fileName)
{
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

        // Fetch new imports.
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

        // Build function to address table.
        addressOfFunctionMap = buildAddressOfFunctionMap(image);

        //##################################################

        unsigned int addressOfBaseImage = image.get_image_base_32() + image.get_base_of_code();  // 0x10000000 + (image.get_base_of_code() or section.get_virtual_address())?
        section_list sections(image.get_image_sections());



        for (section &section : sections) {
            if (section.get_name() == ".text") {
                qDebug() << "Address of base image: " << hex << addressOfBaseImage;

                // Dunia.dll
                unsigned int addressOfGetHostByName = 0x100141FC; // getHostbyname
                unsigned int addressOfGetAdaptersInfo = 0x10C6A692; //getAdaptersInfo

                unsigned char* data = (unsigned char*)section.get_raw_data().c_str();

                //data[addressOfGetHostByName - addressOfBaseImage];

                //const char* data = section.get_raw_data().c_str();

                for (unsigned int i = 0; i < 8; i++) {
                    qDebug() << hex << data[i];
                }
            }
        }

        //##################################################

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

/*
void PeFile::printFunctions(const pe_base &image)
{
    // Print all libraries.
    for (import_library importLibrary : get_imported_functions(image)) {
        if (importLibrary.get_name() == Constants::library_name.toStdString()) {
            qDebug() << "Library:" << QString::fromStdString(importLibrary.get_name());

            // Print all functions.
            for (imported_function function : importLibrary.get_imported_functions()) {
                qDebug() << "Function:" << QString::fromStdString(function.get_name()) << "(VA:" << hex << function.get_iat_va() << ")";
            }
        }
    }
}
*/
