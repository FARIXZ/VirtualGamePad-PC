#include "../gamepadSim.hpp"

#include <QDebug>
#include <cmath>
#include <mutex>
#include <optional>

GamepadInjector::GamepadInjector() : injector(nullptr)
{
	qDebug() << "GamepadInjector constructor called";

	useViGEm = false;
	vigemClient = nullptr;
	vigemTarget = nullptr;
	memset(&vigemReport, 0, sizeof(XUSB_REPORT));

	// 1. Try to initialize ViGEm Bus Emulation first
	qInfo() << "Attempting to initialize ViGEm Client...";
	PVIGEM_CLIENT client = vigem_alloc();
	if (client != nullptr)
	{
		if (VIGEM_SUCCESS(vigem_connect(client)))
		{
			PVIGEM_TARGET target = vigem_target_x360_alloc();
			if (target != nullptr)
			{
				if (VIGEM_SUCCESS(vigem_target_add(client, target)))
				{
					vigemClient = client;
					vigemTarget = target;
					useViGEm = true;
					qInfo() << "ViGEm Bus virtual Xbox 360 controller initialized and connected successfully!";
				}
				else
				{
					qWarning() << "Failed to plug virtual target into ViGEm Bus.";
					vigem_target_free(target);
					vigem_disconnect(client);
					vigem_free(client);
				}
			}
			else
			{
				qWarning() << "Failed to allocate ViGEm target.";
				vigem_disconnect(client);
				vigem_free(client);
			}
		}
		else
		{
			qWarning() << "Failed to connect to ViGEm Bus. Driver might not be installed.";
			vigem_free(client);
		}
	}
	else
	{
		qWarning() << "Failed to allocate ViGEm Client.";
	}

	// 2. Fall back to standard Microsoft InputInjector if ViGEm is not available
	if (!useViGEm)
	{
		qInfo() << "ViGEm not available. Falling back to Microsoft InputInjector...";
		try
		{
			// Initialize gamepadState with default values inside the try-catch fallback block.
			// This prevents unhandled WinRT activation crashes.
			gamepadState = InjectedInputGamepadInfo{};
			gamepadState.Buttons(static_cast<WinRTGamepadButtons>(0));
			gamepadState.LeftThumbstickX(0.0);
			gamepadState.LeftThumbstickY(0.0);
			gamepadState.RightThumbstickX(0.0);
			gamepadState.RightThumbstickY(0.0);
			gamepadState.LeftTrigger(0.0);
			gamepadState.RightTrigger(0.0);

			static std::optional<InputInjector>* pStaticInjector = nullptr;
			static bool isInitialized = false;
			static std::mutex initMutex;

			std::lock_guard<std::mutex> lock(initMutex);

			if (pStaticInjector == nullptr)
			{
				InputInjector createdInjector = InputInjector::TryCreate();
				if (createdInjector == nullptr)
				{
					qCritical() << "Failed to create fallback InputInjector.";
					throw std::runtime_error("Failed to create InputInjector. Please ensure you have Administrator privileges and Developer Mode enabled.");
				}
				pStaticInjector = new std::optional<InputInjector>(createdInjector);
			}

			if (!isInitialized)
			{
				(*pStaticInjector)->InitializeGamepadInjection();
				isInitialized = true;
				qInfo() << "Fallback InputInjector initialized successfully for gamepad injection.";
			}

			injector = **pStaticInjector;
		}
		catch (const winrt::hresult_error &ex)
		{
			qCritical() << "Exception while creating fallback InputInjector:"
						<< QString::fromWCharArray(ex.message().c_str());
			injector = nullptr;
			throw std::runtime_error("WinRT error while creating fallback InputInjector: " +
									 QString::fromWCharArray(ex.message().c_str()).toStdString());
		}
		catch (const std::runtime_error &ex)
		{
			injector = nullptr;
			throw ex;
		}
		catch (...)
		{
			qCritical() << "Unknown exception while creating fallback InputInjector.";
			injector = nullptr;
			throw std::runtime_error("Unknown error while creating fallback InputInjector.");
		}
	}
}

GamepadInjector::~GamepadInjector()
{
	qDebug() << "GamepadInjector destructor called";
	if (useViGEm)
	{
		if (vigemClient && vigemTarget)
		{
			qInfo() << "Removing ViGEm virtual Xbox 360 controller...";
			PVIGEM_CLIENT client = static_cast<PVIGEM_CLIENT>(vigemClient);
			PVIGEM_TARGET target = static_cast<PVIGEM_TARGET>(vigemTarget);
			vigem_target_remove(client, target);
			vigem_target_free(target);
			vigem_disconnect(client);
			vigem_free(client);
		}
		useViGEm = false;
		vigemClient = nullptr;
		vigemTarget = nullptr;
	}
	else
	{
		try
		{
			injector = nullptr;
			qInfo() << "Gamepad fallback injection reference cleared.";
		}
		catch (...)
		{
			qDebug() << "Failed to clear gamepad fallback injection reference";
		}
	}
}

void GamepadInjector::update(const InjectedInputGamepadInfo &state)
{
	if (useViGEm)
	{
		// Map floats (-1.0f to 1.0f) to SHORTs (-32768 to 32767)
		vigemReport.sThumbLX = static_cast<SHORT>(state.LeftThumbstickX() * 32767.0);
		vigemReport.sThumbLY = static_cast<SHORT>(state.LeftThumbstickY() * 32767.0);
		vigemReport.sThumbRX = static_cast<SHORT>(state.RightThumbstickX() * 32767.0);
		vigemReport.sThumbRY = static_cast<SHORT>(state.RightThumbstickY() * 32767.0);

		// Map triggers (0.0 to 1.0) to BYTEs (0 to 255)
		vigemReport.bLeftTrigger = static_cast<BYTE>(state.LeftTrigger() * 255.0);
		vigemReport.bRightTrigger = static_cast<BYTE>(state.RightTrigger() * 255.0);
	}
	else
	{
		gamepadState = state;
	}
}

void GamepadInjector::pressButton(WinRTGamepadButtons button)
{
	if (useViGEm)
	{
		USHORT xButton = 0;
		switch (button)
		{
			case WinRTGamepadButtons::A: xButton = XUSB_GAMEPAD_A; break;
			case WinRTGamepadButtons::B: xButton = XUSB_GAMEPAD_B; break;
			case WinRTGamepadButtons::X: xButton = XUSB_GAMEPAD_X; break;
			case WinRTGamepadButtons::Y: xButton = XUSB_GAMEPAD_Y; break;
			case WinRTGamepadButtons::DPadUp: xButton = XUSB_GAMEPAD_DPAD_UP; break;
			case WinRTGamepadButtons::DPadDown: xButton = XUSB_GAMEPAD_DPAD_DOWN; break;
			case WinRTGamepadButtons::DPadLeft: xButton = XUSB_GAMEPAD_DPAD_LEFT; break;
			case WinRTGamepadButtons::DPadRight: xButton = XUSB_GAMEPAD_DPAD_RIGHT; break;
			case WinRTGamepadButtons::LeftShoulder: xButton = XUSB_GAMEPAD_LEFT_SHOULDER; break;
			case WinRTGamepadButtons::RightShoulder: xButton = XUSB_GAMEPAD_RIGHT_SHOULDER; break;
			case WinRTGamepadButtons::LeftThumbstick: xButton = XUSB_GAMEPAD_LEFT_THUMB; break;
			case WinRTGamepadButtons::RightThumbstick: xButton = XUSB_GAMEPAD_RIGHT_THUMB; break;
			case WinRTGamepadButtons::Menu: xButton = XUSB_GAMEPAD_START; break;
			case WinRTGamepadButtons::View: xButton = XUSB_GAMEPAD_BACK; break;
			default: break;
		}
		vigemReport.wButtons |= xButton;
	}
	else
	{
		gamepadState.Buttons(static_cast<WinRTGamepadButtons>(
			static_cast<uint32_t>(gamepadState.Buttons()) | static_cast<uint32_t>(button)));
	}
}

void GamepadInjector::releaseButton(WinRTGamepadButtons button)
{
	if (useViGEm)
	{
		USHORT xButton = 0;
		switch (button)
		{
			case WinRTGamepadButtons::A: xButton = XUSB_GAMEPAD_A; break;
			case WinRTGamepadButtons::B: xButton = XUSB_GAMEPAD_B; break;
			case WinRTGamepadButtons::X: xButton = XUSB_GAMEPAD_X; break;
			case WinRTGamepadButtons::Y: xButton = XUSB_GAMEPAD_Y; break;
			case WinRTGamepadButtons::DPadUp: xButton = XUSB_GAMEPAD_DPAD_UP; break;
			case WinRTGamepadButtons::DPadDown: xButton = XUSB_GAMEPAD_DPAD_DOWN; break;
			case WinRTGamepadButtons::DPadLeft: xButton = XUSB_GAMEPAD_DPAD_LEFT; break;
			case WinRTGamepadButtons::DPadRight: xButton = XUSB_GAMEPAD_DPAD_RIGHT; break;
			case WinRTGamepadButtons::LeftShoulder: xButton = XUSB_GAMEPAD_LEFT_SHOULDER; break;
			case WinRTGamepadButtons::RightShoulder: xButton = XUSB_GAMEPAD_RIGHT_SHOULDER; break;
			case WinRTGamepadButtons::LeftThumbstick: xButton = XUSB_GAMEPAD_LEFT_THUMB; break;
			case WinRTGamepadButtons::RightThumbstick: xButton = XUSB_GAMEPAD_RIGHT_THUMB; break;
			case WinRTGamepadButtons::Menu: xButton = XUSB_GAMEPAD_START; break;
			case WinRTGamepadButtons::View: xButton = XUSB_GAMEPAD_BACK; break;
			default: break;
		}
		vigemReport.wButtons &= ~xButton;
	}
	else
	{
		gamepadState.Buttons(static_cast<WinRTGamepadButtons>(
			static_cast<uint32_t>(gamepadState.Buttons()) & ~static_cast<uint32_t>(button)));
	}
}

void GamepadInjector::inject()
{
	if (useViGEm)
	{
		if (vigemClient && vigemTarget)
		{
			vigem_target_x360_update(static_cast<PVIGEM_CLIENT>(vigemClient), static_cast<PVIGEM_TARGET>(vigemTarget), vigemReport);
		}
	}
	else if (injector)
	{
		try
		{
			injector.InjectGamepadInput(gamepadState);
		}
		catch (...)
		{
			qDebug() << "Failed to inject gamepad input via fallback injector";
		}
	}
}
