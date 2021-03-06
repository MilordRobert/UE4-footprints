// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "Footprints.h"
#include "Footprints/FootprintTypes.h"
#include "DrawDebugHelpers.h"
#include "Engine.h"
#include "FootprintsCharacter.h"

//////////////////////////////////////////////////////////////////////////
// AFootprintsCharacter

AFootprintsCharacter::AFootprintsCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	// set our turn rates for input
	BaseTurnRate = 45.f;
	BaseLookUpRate = 45.f;

	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true; // Character moves in the direction of input...	
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f); // ...at this rotation rate
	GetCharacterMovement()->JumpZVelocity = 600.f;
	GetCharacterMovement()->AirControl = 0.2f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->AttachTo(RootComponent);
	CameraBoom->TargetArmLength = 300.0f; // The camera follows at this distance behind the character	
	CameraBoom->bUsePawnControlRotation = true; // Rotate the arm based on the controller

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->AttachTo(CameraBoom, USpringArmComponent::SocketName); // Attach the camera to the end of the boom and let the boom adjust to match the controller orientation
	FollowCamera->bUsePawnControlRotation = false; // Camera does not rotate relative to arm

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named MyCharacter (to avoid direct content references in C++)
}

//////////////////////////////////////////////////////////////////////////
// Input

void AFootprintsCharacter::SetupPlayerInputComponent(class UInputComponent* InputComponent)
{
	// Set up gameplay key bindings
	check(InputComponent);
	InputComponent->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);
	InputComponent->BindAction("Jump", IE_Released, this, &ACharacter::StopJumping);

	InputComponent->BindAxis("MoveForward", this, &AFootprintsCharacter::MoveForward);
	InputComponent->BindAxis("MoveRight", this, &AFootprintsCharacter::MoveRight);

	// We have 2 versions of the rotation bindings to handle different kinds of devices differently
	// "turn" handles devices that provide an absolute delta, such as a mouse.
	// "turnrate" is for devices that we choose to treat as a rate of change, such as an analog joystick
	InputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	InputComponent->BindAxis("TurnRate", this, &AFootprintsCharacter::TurnAtRate);
	InputComponent->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);
	InputComponent->BindAxis("LookUpRate", this, &AFootprintsCharacter::LookUpAtRate);

	// handle touch devices
	InputComponent->BindTouch(IE_Pressed, this, &AFootprintsCharacter::TouchStarted);
	InputComponent->BindTouch(IE_Released, this, &AFootprintsCharacter::TouchStopped);
}


void AFootprintsCharacter::TouchStarted(ETouchIndex::Type FingerIndex, FVector Location)
{
	// jump, but only on the first touch
	if (FingerIndex == ETouchIndex::Touch1)
	{
		Jump();
	}
}

void AFootprintsCharacter::TouchStopped(ETouchIndex::Type FingerIndex, FVector Location)
{
	if (FingerIndex == ETouchIndex::Touch1)
	{
		StopJumping();
	}
}

void AFootprintsCharacter::TurnAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerYawInput(Rate * BaseTurnRate * GetWorld()->GetDeltaSeconds());
}

void AFootprintsCharacter::LookUpAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerPitchInput(Rate * BaseLookUpRate * GetWorld()->GetDeltaSeconds());
}

void AFootprintsCharacter::MoveForward(float Value)
{
	if ((Controller != NULL) && (Value != 0.0f))
	{
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		AddMovementInput(Direction, Value);
	}
}

void AFootprintsCharacter::MoveRight(float Value)
{
	if ( (Controller != NULL) && (Value != 0.0f) )
	{
		// find out which way is right
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);
	
		// get right vector 
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		// add movement in that direction
		AddMovementInput(Direction, Value);
	}
}


void AFootprintsCharacter::Trace(FHitResult& OutHit, const FVector& Location) const
{
    FVector Start = Location;
    FVector End = Location;

    Start.Z += 20.0f;
    End.Z -= 20.0f;

    //Re-initialize hit info
    OutHit = FHitResult(ForceInit);

    FCollisionQueryParams TraceParams(FName(TEXT("Footprint trace")), true, this);
    TraceParams.bReturnPhysicalMaterial = true;

    GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, TraceParams);
}

void AFootprintsCharacter::FootDown(const UArrowComponent* FootArrow) const
{
    FHitResult HitResult;
    FVector FootWorldPosition = FootArrow->GetComponentTransform().GetLocation();
    FVector Forward = FootArrow->GetForwardVector();

    Trace(HitResult, FootWorldPosition);    
    UPhysicalMaterial* PhysMat = HitResult.PhysMaterial.Get();

    // Retrieve the particle system and decal object to spawn for our current ground type
    UParticleSystem* ParticleFX = FootprintTypes->GetFootprintFX(PhysMat);
    TSubclassOf<ADecalActor> Decal = FootprintTypes->GetFootprintDecal(PhysMat);
        
    // Create a rotator using the landscape normal and our foot forward vectors
    // Note that we use the function ZX to enforce the normal direction (Z)
    FQuat floorRot = FRotationMatrix::MakeFromZX(HitResult.Normal, Forward).ToQuat();
    FQuat offsetRot(FRotator(0.0f, -90.0f, 0.0f));
    FRotator Rotation = (floorRot * offsetRot).Rotator();

    // Spawn decal and particle emitter
    if(Decal)
        AActor* DecalInstance = GetWorld()->SpawnActor(Decal, &HitResult.Location, &Rotation);
    if(ParticleFX)
        UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), ParticleFX, HitResult.Location);
}