#include <DriverMapper/include/intel_driver.hpp>
#include <DriverMapper/include/kdmapper.hpp>
#include <DriverMapper/bytes/bytes.h>

#include <../Comm.hpp>
#include <../CommClient.hpp>

bool g_driverMapped = false;

inline auto MapDriver( ) -> bool {
	NTSTATUS status = intel_driver::Load( );
	if ( !NT_SUCCESS( status ) ) {
		return false;
	}

	ULONG64 address = reinterpret_cast<ULONG64>(&g_Comm);

	g_CommClient = std::make_unique<CommClient>(&g_Comm);
	g_CommClient->Initialize();

	ULONG64 result = kdmapper::MapDriver(
		( BYTE* ) driver_bytes ,
		GetCurrentProcessId(), address,
		false ,
		true ,
		kdmapper::AllocationMode::AllocatePool ,
		false ,
		nullptr ,
		nullptr ,
		true
	);

	g_driverMapped = ( result != 0 );

	intel_driver::Unload( );

	return g_driverMapped;
}
