// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Reflection;
using UnrealBuildTool;

namespace AutomationTool
{
	/// <summary>
	/// Platform abstraction layer.
	/// </summary>
	public class Platform : CommandUtils
	{
		#region Intialization

		private static Dictionary<TargetPlatformDescriptor, Platform> AllPlatforms = new Dictionary<TargetPlatformDescriptor, Platform>();
		internal static void InitializePlatforms(Assembly[] AssembliesWithPlatforms = null)
		{
			LogVerbose("Creating platforms.");

			// Create all available platforms.
			foreach (var ScriptAssembly in (AssembliesWithPlatforms != null ? AssembliesWithPlatforms : AppDomain.CurrentDomain.GetAssemblies()))
			{
				CreatePlatformsFromAssembly(ScriptAssembly);
			}
			// Create dummy platforms for platforms we don't support
			foreach (var PlatformType in Enum.GetValues(typeof(UnrealTargetPlatform)))
			{
				var TargetDesc = new TargetPlatformDescriptor((UnrealTargetPlatform)PlatformType);
				Platform ExistingInstance;
				if (AllPlatforms.TryGetValue(TargetDesc, out ExistingInstance) == false)
				{
					LogVerbose("Creating placeholder platform for target: {0}", TargetDesc.Type);
					AllPlatforms.Add(TargetDesc, new Platform(TargetDesc.Type));
				}
			}
		}

		private static void CreatePlatformsFromAssembly(Assembly ScriptAssembly)
		{
			LogVerbose("Looking for platforms in {0}", ScriptAssembly.Location);
			Type[] AllTypes = null;
			try
			{
				AllTypes = ScriptAssembly.GetTypes();
			}
			catch (Exception Ex)
			{
				LogError("Failed to get assembly types for {0}", ScriptAssembly.Location);
				if (Ex is ReflectionTypeLoadException)
				{					
					var TypeLoadException = (ReflectionTypeLoadException)Ex;
					if (!IsNullOrEmpty(TypeLoadException.LoaderExceptions))
					{
						LogError("Loader Exceptions:");
						foreach (var LoaderException in TypeLoadException.LoaderExceptions)
						{
							LogError(LogUtils.FormatException(LoaderException));
						}
					}
					else
					{
						LogError("No Loader Exceptions available.");
					}
				}
				// Re-throw, this is still a critical error!
				throw Ex;
			}
			foreach (var PotentialPlatformType in AllTypes)
			{
				if (PotentialPlatformType != typeof(Platform) && typeof(Platform).IsAssignableFrom(PotentialPlatformType) && !PotentialPlatformType.IsAbstract)
				{
					LogVerbose("Creating platform {0} from {1}.", PotentialPlatformType.Name, ScriptAssembly.Location);
					var PlatformInstance = Activator.CreateInstance(PotentialPlatformType) as Platform;
                    var PlatformDesc = PlatformInstance.GetTargetPlatformDescriptor();

                    Platform ExistingInstance;
                    if (!AllPlatforms.TryGetValue(PlatformDesc, out ExistingInstance))
					{
						AllPlatforms.Add(PlatformDesc, PlatformInstance);
					}
					else
					{
						LogWarning("Platform {0} already exists", PotentialPlatformType.Name);
					}
				}
			}
		}

		#endregion

		protected UnrealTargetPlatform TargetPlatformType = UnrealTargetPlatform.Unknown;
		protected UnrealTargetPlatform TargetIniPlatformType = UnrealTargetPlatform.Unknown;

		public Platform(UnrealTargetPlatform PlatformType)
		{
			TargetPlatformType = PlatformType;
			TargetIniPlatformType = PlatformType;
		}

		/// <summary>
		/// Allow the platform to alter the ProjectParams
		/// </summary>
		/// <param name="ProjParams"></param>
		public virtual void PlatformSetupParams(ref ProjectParams ProjParams)
		{

		}

        public virtual TargetPlatformDescriptor GetTargetPlatformDescriptor()
        {
            return new TargetPlatformDescriptor(TargetPlatformType, "");
        }

        /// <summary>
        /// Package files for the current platform.
        /// </summary>
        /// <param name="ProjectPath"></param>
        /// <param name="ProjectExeFilename"></param>
        public virtual void Package(ProjectParams Params, DeploymentContext SC, int WorkingCL)
		{
			throw new AutomationException("{0} does not yet implement Packaging.", PlatformType);
		}

        /// <summary>
        /// Does the reverse of the output from the package process
        /// </summary>
        /// <param name="SourcePath"></param>
        /// <param name="DestinationPath"></param>
        public virtual void ExtractPackage(ProjectParams Params, string SourcePath, string DestinationPath)
        {
            throw new AutomationException("{0} does not yet implement ExtractPackage.", PlatformType);
        }

		/// <summary>
		/// Allow platform to do platform specific work on archived project before it's deployed.
		/// </summary>
		/// <param name="Params"></param>
		/// <param name="SC"></param>
		public virtual void ProcessArchivedProject(ProjectParams Params, DeploymentContext SC)
		{
		}

        /// <summary>
        /// Get all connected device names for this platform
        /// </summary>
        /// <param name="Params"></param>
        /// <param name="SC"></param>
        public virtual void GetConnectedDevices(ProjectParams Params, out List<string> Devices)
        {
            Devices = null;
            LogWarning("{0} does not implement GetConnectedDevices", PlatformType);
        }



		/// <summary>
		/// Deploy the application on the current platform
		/// </summary>
		/// <param name="Params"></param>
		/// <param name="SC"></param>
		public virtual void Deploy(ProjectParams Params, DeploymentContext SC)
		{
			LogWarning("{0} does not implement Deploy...", PlatformType);
		}

		/// <summary>
		/// Run the client application on the platform
		/// </summary>
		/// <param name="ClientRunFlags"></param>
		/// <param name="ClientApp"></param>
		/// <param name="ClientCmdLine"></param>
		public virtual ProcessResult RunClient(ERunOptions ClientRunFlags, string ClientApp, string ClientCmdLine, ProjectParams Params)
		{
			PushDir(Path.GetDirectoryName(ClientApp));
			// Always start client process and don't wait for exit.
			ProcessResult ClientProcess = Run(ClientApp, ClientCmdLine, null, ClientRunFlags | ERunOptions.NoWaitForExit);
			PopDir();

			return ClientProcess;
		}

		/// <summary>
		/// Allow platform specific clean-up or detection after client has run
		/// </summary>
		/// <param name="ClientRunFlags"></param>
		public virtual void PostRunClient(ProcessResult Result, ProjectParams Params)
		{
			// do nothing in the default case
		}

		public virtual void UploadSymbols(ProjectParams Params, DeploymentContext SC)
		{
			Log("{0} does not implement UploadSymbols...", PlatformType);
		}

		/// <summary>
		/// Get the platform-specific name for the executable (with out the file extension)
		/// </summary>
		/// <param name="InExecutableName"></param>
		/// <returns></returns>
		public virtual string GetPlatformExecutableName(string InExecutableName)
		{
			return InExecutableName;
		}

		public virtual List<string> GetExecutableNames(DeploymentContext SC, bool bIsRun = false)
		{
			var ExecutableNames = new List<String>();
			string Ext = AutomationTool.Platform.GetExeExtension(SC.StageTargetPlatform.TargetPlatformType);
			if (!String.IsNullOrEmpty(SC.CookPlatform))
			{
				if (SC.StageExecutables.Count() > 0)
				{
					foreach (var StageExecutable in SC.StageExecutables)
					{
						string ExeName = SC.StageTargetPlatform.GetPlatformExecutableName(StageExecutable);
						if (!SC.IsCodeBasedProject)
						{
							ExecutableNames.Add(CombinePaths(SC.RuntimeRootDir, "Engine/Binaries", SC.PlatformDir, ExeName + Ext));
						}
						else
						{
							ExecutableNames.Add(CombinePaths(SC.RuntimeProjectRootDir, "Binaries", SC.PlatformDir, ExeName + Ext));
						}
					}
				}
				//@todo, probably the rest of this can go away once everything passes it through
				else if (SC.DedicatedServer)
				{
                    if (!SC.IsCodeBasedProject)
                    {
						string ExeName = SC.StageTargetPlatform.GetPlatformExecutableName("UE4Server");
						ExecutableNames.Add(CombinePaths(SC.RuntimeRootDir, "Engine/Binaries", SC.PlatformDir, ExeName + Ext));
					}
					else
					{
						string ExeName = SC.StageTargetPlatform.GetPlatformExecutableName(SC.ShortProjectName + "Server");
						string ClientApp = CombinePaths(SC.RuntimeProjectRootDir, "Binaries", SC.PlatformDir, ExeName + Ext);
						var TestApp = CombinePaths(SC.ProjectRoot, "Binaries", SC.PlatformDir, SC.ShortProjectName + "Server" + Ext);
						string Game = "Game";
						//@todo, this is sketchy, someone might ask what the exe is before it is compiled
						if (!FileExists_NoExceptions(ClientApp) && !FileExists_NoExceptions(TestApp) && SC.ShortProjectName.EndsWith(Game, StringComparison.InvariantCultureIgnoreCase))
						{
							ExeName = SC.StageTargetPlatform.GetPlatformExecutableName(SC.ShortProjectName.Substring(0, SC.ShortProjectName.Length - Game.Length) + "Server");
							ClientApp = CombinePaths(SC.RuntimeProjectRootDir, "Binaries", SC.PlatformDir, ExeName + Ext);
						}
						ExecutableNames.Add(ClientApp);
					}
				}
				else
				{
                    if (!SC.IsCodeBasedProject)
                    {
						string ExeName = SC.StageTargetPlatform.GetPlatformExecutableName("UE4Game");
						ExecutableNames.Add(CombinePaths(SC.RuntimeRootDir, "Engine/Binaries", SC.PlatformDir, ExeName + Ext));
					}
					else
					{
						string ExeName = SC.StageTargetPlatform.GetPlatformExecutableName(SC.ShortProjectName);
						ExecutableNames.Add(CombinePaths(SC.RuntimeProjectRootDir, "Binaries", SC.PlatformDir, ExeName + Ext));
					}
				}
			}
			else
			{
				string ExeName = SC.StageTargetPlatform.GetPlatformExecutableName("UE4Editor");
				ExecutableNames.Add(CombinePaths(SC.RuntimeRootDir, "Engine/Binaries", SC.PlatformDir, ExeName + Ext));
			}
			return ExecutableNames;
		}

		/// <summary>
		/// Get the files to deploy, specific to this platform, typically binaries
		/// </summary>
		/// <param name="SC">Deployment Context</param>
		public virtual void GetFilesToDeployOrStage(ProjectParams Params, DeploymentContext SC)
		{
			throw new AutomationException("{0} does not yet implement GetFilesToDeployOrStage.", PlatformType);
		}

		/// <summary>
		/// Called after CopyUsingStagingManifest.  Does anything platform specific that requires a final list of staged files.
		/// e.g.  PlayGo emulation control file generation for PS4.
		/// </summary>
		/// <param name="Params"></param>
		/// <param name="SC"></param>
		public virtual void PostStagingFileCopy(ProjectParams Params, DeploymentContext SC)
		{
		}

		/// <summary>
		/// Get the files to deploy, specific to this platform, typically binaries
		/// </summary>
		/// <param name="SC">Deployment Context</param>
		public virtual void GetFilesToArchive(ProjectParams Params, DeploymentContext SC)
		{
			SC.ArchiveFiles(SC.StageDirectory);
		}

		/// <summary>
		/// Gets cook platform name for this platform.
		/// </summary>
		/// <param name="bDedicatedServer">True if cooking for dedicated server</param>
		/// <param name="bIsClientOnly">True if cooking for client only</param>
		/// <returns>Cook platform string.</returns>
		public virtual string GetCookPlatform(bool bDedicatedServer, bool bIsClientOnly)
		{
			throw new AutomationException("{0} does not yet implement GetCookPlatform.", PlatformType);
		}

        /// <summary>
        /// Gets extra cook commandline arguments for this platform.
        /// </summary>
        /// <param name="Params"> ProjectParams </param>
        /// <returns>Cook platform string.</returns>
        public virtual string GetCookExtraCommandLine(ProjectParams Params)
        {
            return ""; 
        }

        /// <summary>
        /// Gets extra maps needed on this platform.
        /// </summary>
        /// <returns>extra maps</returns>
        public virtual List<string> GetCookExtraMaps()
        {
            return new List<string>();
        }

        /// <summary>
        /// Get a release pak file path, if we are currently building a patch then get the previous release pak file path, if we are creating a new release this will be the output path
        /// </summary>
        /// <param name="SC"></param>
        /// <param name="Params"></param>
        /// <param name="PakName"></param>
        /// <returns></returns>
        public virtual string GetReleasePakFilePath(DeploymentContext SC, ProjectParams Params, string PakName)
        {
            if (Params.IsGeneratingPatch)
            {
                return CombinePaths(Params.GetBasedOnReleaseVersionPath(SC), PakName);
            }
            else
            {
                return CombinePaths(Params.GetCreateReleaseVersionPath(SC), PakName);
            }        
        }

        /// <summary>
        /// Gets editor cook platform name for this platform. Cooking the editor is not useful, but this is used to fill the derived data cache
        /// </summary>
        /// <returns>Cook platform string.</returns>
        public virtual string GetEditorCookPlatform()
        {
            return GetCookPlatform(false, false);
        }

		/// <summary>
		/// return true if we need to change the case of filenames inside of pak files
		/// </summary>
		/// <returns></returns>
		public virtual bool DeployPakInternalLowerCaseFilenames()
		{
			return false;
		}

		/// <summary>
		/// return true if we need to change the case of filenames outside of pak files
		/// </summary>
		/// <returns></returns>
		public virtual bool DeployLowerCaseFilenames(bool bUFSFile)
		{
			return false;
		}

		/// <summary>
		/// Converts local path to target platform path.
		/// </summary>
		/// <param name="LocalPath">Local path.</param>
		/// <param name="LocalRoot">Local root.</param>
		/// <returns>Local path converted to device format path.</returns>
		public virtual string LocalPathToTargetPath(string LocalPath, string LocalRoot)
		{
			return LocalPath;
		}
        /// <summary>
        /// Returns a list of the compiler produced debug file extensions
        /// </summary>
        /// <returns>a list of the compiler produced debug file extensions</returns>
        public virtual List<string> GetDebugFileExtentions()
        {
            return new List<string>();
        }

		/// <summary>
		/// Remaps movie directory for platforms that need a remap
		/// </summary>
		public virtual bool StageMovies
		{
			get { return true; }
		}

		/// <summary>
		/// UnrealTargetPlatform type for this platform.
		/// </summary>
		public UnrealTargetPlatform PlatformType
		{
			get { return TargetPlatformType; }
		}

		/// <summary>
		/// UnrealTargetPlatform type for this platform.
		/// </summary>
		public UnrealTargetPlatform IniPlatformType
		{
			get { return TargetIniPlatformType; }
		}

		/// <summary>
		/// True if this platform is supported.
		/// </summary>
		public virtual bool IsSupported
		{
			get { return false; }
		}

		/// <summary>
		/// True if this platform requires UFE for deploying
		/// </summary>
		public virtual bool DeployViaUFE
		{
			get { return false; }
		}

		/// <summary>
		/// True if this platform requires UFE for launching
		/// </summary>
		public virtual bool LaunchViaUFE
		{
			get { return false; }
		}

		/// <summary>
		/// True if this platform can write to the abslog path that's on the host desktop.
		/// </summary>
		public virtual bool UseAbsLog
		{
			get { return true; }
		}

		/// <summary>
		/// Remaps the given content directory to its final location
		/// </summary>
		public virtual string Remap(string Dest)
		{
			return Dest;
		}

		/// <summary>
		/// Tri-state - The intent is to override command line parameters for pak if needed per platform.
		/// </summary>
        /// 
        public enum PakType { Always, Never, DontCare };

        public virtual PakType RequiresPak(ProjectParams Params)
		{
            return PakType.DontCare;
		}

        /// <summary>
        /// Returns platform specific command line options for UnrealPak
        /// </summary>
        public virtual string GetPlatformPakCommandLine()
        {
            return "";
        }

        /// <summary>
        /// True if this platform is supported.
        /// </summary>
        public virtual bool SupportsMultiDeviceDeploy
        {
            get { return false; }
        }

        /// <summary>
        /// Returns true if the platform wants patches to generate a small .pak file containing the difference
        /// of current data against a shipped pak file.
        /// </summary>
        /// <returns></returns>
        public virtual bool GetPlatformPatchesWithDiffPak(ProjectParams Params, DeploymentContext SC)
		{
			return true;
		}

		/// <summary>
		///  Returns whether the platform requires a package to deploy to a device
		/// </summary>
		public virtual bool RequiresPackageToDeploy
		{
			get { return false; }
		}

		public virtual List<string> GetFilesForCRCCheck()
		{
			string CmdLine = "UE4CommandLine.txt";
			if (DeployLowerCaseFilenames(true))
			{
				CmdLine = CmdLine.ToLowerInvariant();
			}
			return new List<string>() { CmdLine };
		}

		#region Hooks

		public virtual void PreBuildAgenda(UE4Build Build, UE4Build.BuildAgenda Agenda)
		{

		}

		public virtual void PostBuildTarget(UE4Build Build, FileReference UProjectPath, string TargetName, string Config)
		{

		}

		/// <summary>
		/// General purpose command to run generic string commands inside the platform interfeace
		/// </summary>
		/// <param name="Command"></param>
		public virtual int RunCommand(string Command)
		{
			return 0;
		}

		public virtual bool ShouldUseManifestForUBTBuilds(string AddArgs)
		{
			return true;
		}

		/// <summary>
		/// Determines whether we should stage a UE4CommandLine.txt for this platform 
		/// </summary>
		public virtual bool ShouldStageCommandLine(ProjectParams Params, DeploymentContext SC)
		{
			return true;
		}

        /// <summary>
        /// Only relevant for the mac and PC at the moment. Example calling the Mac platform with PS4 as an arg will return false. Can't compile or cook for the PS4 on the mac.
        /// </summary>
        public virtual bool CanHostPlatform(UnrealTargetPlatform Platform)
        {
            return false;
        }

		/// <summary>
		/// Allows some platforms to not be compiled, for instance when BuildCookRun -build is performed
		/// </summary>
		/// <returns><c>true</c> if this instance can be compiled; otherwise, <c>false</c>.</returns>
		public virtual bool CanBeCompiled()
		{
			return true;
		}

		public virtual bool RetrieveDeployedManifests(ProjectParams Params, DeploymentContext SC, string DeviceName, out List<string> UFSManifests, out List<string> NonUFSManifests)
		{
			UFSManifests = null;
			NonUFSManifests = null;
			return false;
		}

		public virtual bool SignExecutables(DeploymentContext SC, ProjectParams Params)
		{
			return true;
		}

		public virtual UnrealTargetPlatform[] GetStagePlatforms()
		{
			return new UnrealTargetPlatform[] { PlatformType };
		}

		#endregion

		#region Utilities

		public static string GetExeExtension(UnrealTargetPlatform Target)
		{
			switch (Target)
			{
				case UnrealTargetPlatform.Win32:
				case UnrealTargetPlatform.Win64:
				case UnrealTargetPlatform.XboxOne:
					return ".exe";
				case UnrealTargetPlatform.PS4:
					return ".self";
				case UnrealTargetPlatform.IOS:
					return ".stub";
				case UnrealTargetPlatform.Linux:
					return "";
				case UnrealTargetPlatform.HTML5:
					return ".js";
			}
			return String.Empty;
		}

		public static Dictionary<TargetPlatformDescriptor, Platform> Platforms
		{
			get { return AllPlatforms; }
		}

        public static Platform GetPlatform(UnrealTargetPlatform PlatformType)
        {
            TargetPlatformDescriptor Desc = new TargetPlatformDescriptor(PlatformType);
            return AllPlatforms[Desc];
        }

        public static Platform GetPlatform(UnrealTargetPlatform PlatformType, string CookFlavor)
        {
            TargetPlatformDescriptor Desc = new TargetPlatformDescriptor(PlatformType, CookFlavor);
            return AllPlatforms[Desc];
        }

        public static bool IsValidTargetPlatform(TargetPlatformDescriptor PlatformDesc)
        {
            return AllPlatforms.ContainsKey(PlatformDesc);
        }

        public static List<TargetPlatformDescriptor> GetValidTargetPlatforms(UnrealTargetPlatform PlatformType, List<string> CookFlavors)
        {
            List<TargetPlatformDescriptor> ValidPlatforms = new List<TargetPlatformDescriptor>();
            if (!CommandUtils.IsNullOrEmpty(CookFlavors))
            {
                foreach (string CookFlavor in CookFlavors)
                {
                    TargetPlatformDescriptor TargetDesc = new TargetPlatformDescriptor(PlatformType, CookFlavor);
                    if (IsValidTargetPlatform(TargetDesc))
                    {
                        ValidPlatforms.Add(TargetDesc);
                    }
                }
            }

            // In case there are no flavors specified or this platform type does not care/support flavors add it as generic platform 
            if (ValidPlatforms.Count == 0)
            {
                TargetPlatformDescriptor TargetDesc = new TargetPlatformDescriptor(PlatformType);
                if (IsValidTargetPlatform(TargetDesc))
                {
                    ValidPlatforms.Add(TargetDesc);
                }
            }

            return ValidPlatforms;
        }

		#endregion
	}
}
