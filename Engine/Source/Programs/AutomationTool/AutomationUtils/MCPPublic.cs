// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.IO;
using AutomationTool;
using System.Runtime.Serialization;
using System.Net;
using System.Reflection;
using System.Text.RegularExpressions;
using UnrealBuildTool;

namespace EpicGames.MCP.Automation
{
	using EpicGames.MCP.Config;
	using System.Threading.Tasks;

    /// <summary>
    /// Utility class to provide commit/rollback functionality via an RAII-like functionality.
    /// Usage is to provide a rollback action that will be called on Dispose if the Commit() method is not called.
    /// This is expected to be used from within a using() ... clause.
    /// </summary>
    public class CommitRollbackTransaction : IDisposable
    {
        /// <summary>
        /// Track whether the transaction will be committed.
        /// </summary>
        private bool IsCommitted = false;

        /// <summary>
        /// 
        /// </summary>
        private System.Action RollbackAction;

        /// <summary>
        /// Call when you want to commit your transaction. Ensures the Rollback action is not called on Dispose().
        /// </summary>
        public void Commit()
        {
            IsCommitted = true;
        }

        /// <summary>
        /// Constructor
        /// </summary>
        /// <param name="RollbackAction">Action to be executed to rollback the transaction.</param>
        public CommitRollbackTransaction(System.Action InRollbackAction)
        {
            RollbackAction = InRollbackAction;
        }

        /// <summary>
        /// Rollback the transaction if its not committed on Dispose.
        /// </summary>
        public void Dispose()
        {
            if (!IsCommitted)
            {
                RollbackAction();
            }
        }
    }

    /// <summary>
    /// Enum that defines the MCP backend-compatible platform
    /// </summary>
    public enum MCPPlatform
    {
        /// <summary>
		/// MCP uses Windows for Win64
        /// </summary>
        Windows,

        /// <summary>
		/// 32 bit Windows
		/// </summary>
		Win32,

		/// <summary>
        /// Mac platform.
        /// </summary>
        Mac,

		/// <summary>
		/// Linux platform.
		/// </summary>
		Linux
    }

    /// <summary>
    /// Enum that defines CDN types
    /// </summary>
    public enum CDNType
    {
        /// <summary>
        /// Internal HTTP CDN server
        /// </summary>
        Internal,

        /// <summary>
        /// Production HTTP CDN server
        /// </summary>
        Production,
    }

    /// <summary>
    /// Class that holds common state used to control the BuildPatchTool build commands that chunk and create patching manifests and publish build info to the BuildInfoService.
    /// </summary>
    public class BuildPatchToolStagingInfo
    {
        /// <summary>
        /// The currently running command, used to get command line overrides
        /// </summary>
        public BuildCommand OwnerCommand;
        /// <summary>
        /// name of the app. Can't always use this to define the staging dir because some apps are not staged to a place that matches their AppName.
        /// </summary>
        public readonly string AppName;
        /// <summary>
        /// Usually the base name of the app. Used to get the MCP key from a branch dictionary. 
        /// </summary>
        public readonly string McpConfigKey;
        /// <summary>
        /// ID of the app (needed for the BuildPatchTool)
        /// </summary>
        public readonly int AppID;
        /// <summary>
        /// BuildVersion of the App we are staging.
        /// </summary>
        public readonly string BuildVersion;
        /// <summary>
        /// Directory where builds will be staged. Rooted at the BuildRootPath, using a subfolder passed in the ctor, 
        /// and using BuildVersion/PlatformName to give each builds their own home.
        /// </summary>
        public readonly string StagingDir;
        /// <summary>
        /// Path to the CloudDir where chunks will be written (relative to the BuildRootPath)
        /// This is used to copy to the web server, so it can use the same relative path to the root build directory.
        /// This allows file to be either copied from the local file system or the webserver using the same relative paths.
        /// </summary>
        public readonly string CloudDirRelativePath;
        /// <summary>
        /// full path to the CloudDir where chunks and manifests should be staged. Rooted at the BuildRootPath, using a subfolder pass in the ctor.
        /// </summary>
        public readonly string CloudDir;
        /// <summary>
        /// Platform we are staging for.
        /// </summary>
        public readonly MCPPlatform Platform;

        /// <summary>
        /// Gets the base filename of the manifest that would be created by invoking the BuildPatchTool with the given parameters.
        /// </summary>
        public virtual string ManifestFilename
        {
            get
            {
				if (!string.IsNullOrEmpty(_ManifestFilename))
				{
					return _ManifestFilename;
				}
				var BaseFilename = string.Format("{0}{1}-{2}.manifest",
					AppName,
					BuildVersion,
					Platform.ToString());
                return Regex.Replace(BaseFilename, @"\s+", ""); // Strip out whitespace in order to be compatible with BuildPatchTool
            }
        }

        /// <summary>
		/// If set, this allows us to over-ride the automatically constructed ManifestFilename
		/// </summary>
		protected string _ManifestFilename;

        /// <summary>
		/// Determine the platform name
        /// </summary>
        static public MCPPlatform ToMCPPlatform(UnrealTargetPlatform TargetPlatform)
        {
            if (TargetPlatform != UnrealTargetPlatform.Win64 && TargetPlatform != UnrealTargetPlatform.Win32 && TargetPlatform != UnrealTargetPlatform.Mac && TargetPlatform != UnrealTargetPlatform.Linux)
            {
                throw new AutomationException("Platform {0} is not properly supported by the MCP backend yet", TargetPlatform);
            }

			if (TargetPlatform == UnrealTargetPlatform.Win64)
			{
				return MCPPlatform.Windows;
			}
			else if (TargetPlatform == UnrealTargetPlatform.Win32)
			{
				return MCPPlatform.Win32;
			}
			else if (TargetPlatform == UnrealTargetPlatform.Mac)
			{
				return MCPPlatform.Mac;
			}

			return MCPPlatform.Linux;
        }
        /// <summary>
		/// Determine the platform name
        /// </summary>
        static public UnrealTargetPlatform FromMCPPlatform(MCPPlatform TargetPlatform)
        {
			if (TargetPlatform != MCPPlatform.Windows && TargetPlatform != MCPPlatform.Win32 && TargetPlatform != MCPPlatform.Mac && TargetPlatform != MCPPlatform.Linux)
            {
                throw new AutomationException("Platform {0} is not properly supported by the MCP backend yet", TargetPlatform);
            }

			if (TargetPlatform == MCPPlatform.Windows)
			{
				return UnrealTargetPlatform.Win64;
			}
			else if (TargetPlatform == MCPPlatform.Win32)
			{
				return UnrealTargetPlatform.Win32;
			}
			else if (TargetPlatform == MCPPlatform.Mac)
			{
				return UnrealTargetPlatform.Mac;
			}

			return UnrealTargetPlatform.Linux;
        }
        /// <summary>
        /// Returns the build root path (P:\Builds on build machines usually)
        /// </summary>
        /// <returns></returns>
        static public string GetBuildRootPath()
        {
            return CommandUtils.P4Enabled && CommandUtils.AllowSubmit
                ? CommandUtils.RootBuildStorageDirectory()
                : CommandUtils.CombinePaths(CommandUtils.CmdEnv.LocalRoot, "LocalBuilds");
        }

        /// <summary>
        /// Basic constructor. 
        /// </summary>
        /// <param name="InAppName"></param>
        /// <param name="InAppID"></param>
        /// <param name="InBuildVersion"></param>
        /// <param name="platform"></param>
        /// <param name="stagingDirRelativePath">Relative path from the BuildRootPath where files will be staged. Commonly matches the AppName.</param>
		/// <param name="InManifestFilename">If specifies, will override the value returned by the ManifestFilename property</param>
        public BuildPatchToolStagingInfo(BuildCommand InOwnerCommand, string InAppName, string InMcpConfigKey, int InAppID, string InBuildVersion, UnrealTargetPlatform platform, string stagingDirRelativePath)
            : this(InOwnerCommand, InAppName, InMcpConfigKey, InAppID, InBuildVersion, ToMCPPlatform(platform), stagingDirRelativePath)
        {
        }

        /// <summary>
        /// Basic constructor. 
        /// </summary>
        /// <param name="InAppName"></param>
        /// <param name="InAppID"></param>
        /// <param name="InBuildVersion"></param>
        /// <param name="platform"></param>
        /// <param name="stagingDirRelativePath">Relative path from the BuildRootPath where files will be staged. Commonly matches the AppName.</param>
		public BuildPatchToolStagingInfo(BuildCommand InOwnerCommand, string InAppName, string InMcpConfigKey, int InAppID, string InBuildVersion, MCPPlatform platform, string stagingDirRelativePath)
        {
            OwnerCommand = InOwnerCommand;
            AppName = InAppName;
			_ManifestFilename = null;
            McpConfigKey = InMcpConfigKey;
            AppID = InAppID;
            BuildVersion = InBuildVersion;
            Platform = platform;
            var BuildRootPath = GetBuildRootPath();
            StagingDir = CommandUtils.CombinePaths(BuildRootPath, stagingDirRelativePath, BuildVersion, Platform.ToString());
            CloudDirRelativePath = CommandUtils.CombinePaths(stagingDirRelativePath, "CloudDir");
            CloudDir = CommandUtils.CombinePaths(BuildRootPath, CloudDirRelativePath);
        }

		/// <summary>
		/// Basic constructor with staging dir suffix override, basically to avoid having platform concatenated
		/// </summary>
		public BuildPatchToolStagingInfo(BuildCommand InOwnerCommand, string InAppName, string InMcpConfigKey, int InAppID, string InBuildVersion, UnrealTargetPlatform platform, string stagingDirRelativePath, string stagingDirSuffix, string InManifestFilename)
			: this(InOwnerCommand, InAppName, InMcpConfigKey, InAppID, InBuildVersion, ToMCPPlatform(platform), stagingDirRelativePath, stagingDirSuffix, InManifestFilename)
		{
		}

		/// <summary>
		/// Basic constructor with staging dir suffix override, basically to avoid having platform concatenated
		/// </summary>
		public BuildPatchToolStagingInfo(BuildCommand InOwnerCommand, string InAppName, string InMcpConfigKey, int InAppID, string InBuildVersion, MCPPlatform platform, string stagingDirRelativePath, string stagingDirSuffix, string InManifestFilename)
		{
			OwnerCommand = InOwnerCommand;
			AppName = InAppName;
			_ManifestFilename = InManifestFilename;
			McpConfigKey = InMcpConfigKey;
			AppID = InAppID;
			BuildVersion = InBuildVersion;
			Platform = platform;
			var BuildRootPath = GetBuildRootPath();
			StagingDir = CommandUtils.CombinePaths(BuildRootPath, stagingDirRelativePath, BuildVersion, stagingDirSuffix);
			CloudDirRelativePath = CommandUtils.CombinePaths(stagingDirRelativePath, "CloudDir");
			CloudDir = CommandUtils.CombinePaths(BuildRootPath, CloudDirRelativePath);
		}

		/// <summary>
		/// Constructor which supports being able to just simply call BuildPatchToolBase.Get().Execute
		/// </summary>
		public BuildPatchToolStagingInfo(BuildCommand InOwnerCommand, string InAppName, int InAppID, string InBuildVersion, MCPPlatform InPlatform, string InCloudDir)
		{
			OwnerCommand = InOwnerCommand;
			AppName = InAppName;
			AppID = InAppID;
			BuildVersion = InBuildVersion;
			Platform = InPlatform;
			CloudDir = InCloudDir;
		}

		/// <summary>
		/// Constructor which sets all values directly, without assuming any default paths.
		/// </summary>
		public BuildPatchToolStagingInfo(BuildCommand InOwnerCommand, string InAppName, int InAppID, string InBuildVersion, MCPPlatform InPlatform, DirectoryReference InStagingDir, DirectoryReference InCloudDir)
		{
			OwnerCommand = InOwnerCommand;
			AppName = InAppName;
			AppID = InAppID;
			BuildVersion = InBuildVersion;
			Platform = InPlatform;
			if(InStagingDir != null)
			{
				StagingDir = InStagingDir.FullName;
			}
			if(InCloudDir != null)
			{
				DirectoryReference BuildRootDir = new DirectoryReference(GetBuildRootPath());
				if(!InCloudDir.IsUnderDirectory(BuildRootDir))
				{
					throw new AutomationException("Cloud directory must be under build root path ({0})", BuildRootDir.FullName);
				}
				CloudDir = InCloudDir.FullName;
				CloudDirRelativePath = InCloudDir.MakeRelativeTo(BuildRootDir).Replace('\\', '/');
			}
		}
    }

	/// <summary>
	/// Class that provides programmatic access to the BuildPatchTool
	/// </summary>
	public abstract class BuildPatchToolBase
	{
		/// <summary>
		/// Obsolete: BuildPatchTool will now only use chunked patch generation.
		/// </summary>
		public enum ChunkType { Chunk }

		/// <summary>
		/// Controls which version of BPT to use when executing.
		/// </summary>
		public enum ToolVersion
		{
			/// <summary>
			/// The current live, tested build.
			/// </summary>
			Live,
			/// <summary>
			/// The latest published build, may be untested.
			/// </summary>
			Next,
			/// <summary>
			/// Use local build from source of BuildPatchTool.
			/// </summary>
			Source
		}

		public class PatchGenerationOptions
		{
			/// <summary>
			/// By default, we will only consider data referenced from manifests modified within five days to be reusable.
			/// </summary>
			private const int DEFAULT_DATA_AGE_THRESHOLD = 5;

			public PatchGenerationOptions()
			{
				DataAgeThreshold = DEFAULT_DATA_AGE_THRESHOLD;
			}

			/// <summary>
			/// Staging information
			/// </summary>
			public BuildPatchToolStagingInfo StagingInfo;
			/// <summary>
			/// The directory containing the build image to be read.
			/// </summary>
			public string BuildRoot;
			/// <summary>
			/// A path to a text file containing BuildRoot relative files, separated by \r\n line endings, to not be included in the build.
			/// </summary>
			public string FileIgnoreList;
			/// <summary>
			/// A path to a text file containing quoted BuildRoot relative files followed by optional attributes such as readonly compressed executable tag:mytag, separated by \r\n line endings.
			/// These attribute will be applied when build is installed client side.
			/// </summary>
			public string FileAttributeList;
			/// <summary>
			/// The path to the app executable, must be relative to, and inside of BuildRoot.
			/// </summary>
			public string AppLaunchCmd;
			/// <summary>
			/// The commandline to send to the app on launch.
			/// </summary>
			public string AppLaunchCmdArgs;
			/// <summary>
			/// The prerequisites installer to launch on successful product install, must be relative to, and inside of BuildRoot.
			/// </summary>
			public string PrereqPath;
			/// <summary>
			/// The commandline to send to prerequisites installer on launch.
			/// </summary>
			public string PrereqArgs;
			/// <summary>
			/// An override for the output manifest filename.
			/// </summary>
			public string OutputFilename;
			/// <summary>
			/// Used as part of manifest filename and build version strings.
			/// </summary>
			public MCPPlatform Platform;
			/// <summary>
			/// When identifying existing patch data to reuse in this build, only
			/// files referenced from a manifest file modified within this number of days will be considered for reuse.
			/// IMPORTANT: This should always be smaller than the minimum age at which manifest files can be deleted by any cleanup process, to ensure
			/// that we do not reuse any files which could be deleted by a concurrently running compactify. It is recommended that this number be at least
			/// two days less than the cleanup data age threshold.
			/// </summary>
			public int DataAgeThreshold;
			/// <summary>
			/// Contains a list of custom string arguments to be embedded in the generated manifest file.
			/// </summary>
			public List<KeyValuePair<string, string>> CustomStringArgs;
			/// <summary>
			/// Contains a list of custom integer arguments to be embedded in the generated manifest file.
			/// </summary>
			public List<KeyValuePair<string, int>> CustomIntArgs;
			/// <summary>
			/// Contains a list of custom float arguments to be embedded in the generated manifest file.
			/// </summary>
			public List<KeyValuePair<string, float>> CustomFloatArgs;

			/// <summary>
			/// Obsolete: BuildPatchTool will now only use chunked patch generation.
			/// </summary>
			public ChunkType AppChunkType;
		}

		/// <summary>
		/// Represents the options passed to the compactify process
		/// </summary>
		public class CompactifyOptions
		{
			private const int DEFAULT_DATA_AGE_THRESHOLD = 2;

			public CompactifyOptions()
			{
				DataAgeThreshold = DEFAULT_DATA_AGE_THRESHOLD;
			}

			/// <summary>
			/// BuildPatchTool will run a compactify on this directory.
			/// </summary>
			public string CompactifyDirectory;
			/// <summary>
			/// Corresponds to the -preview parameter
			/// </summary>
			public bool bPreviewCompactify;
			/// <summary>
			/// Patch data files modified within this number of days will *not* be deleted, to ensure that any patch files being written out by a.
			/// patch generation process are not deleted before their corresponding manifest file(s) can be written out.
			/// NOTE: this should be set to a value larger than the expected maximum time that a build could take.
			/// </summary>
			public int DataAgeThreshold;
		}

		public class DataEnumerationOptions
		{
			/// <summary>
			/// The file path to the manifest to enumerate from.
			/// </summary>
			public string ManifestFile;
			/// <summary>
			/// The file path to where the list will be saved out, containing \r\n separated cloud relative file paths.
			/// </summary>
			public string OutputFile;
			/// <summary>
			/// When true, the output will include the size of each file on each line, separated by \t
			/// </summary>
			public bool bIncludeSize;
		}

		public class ManifestMergeOptions
		{
			/// <summary>
			/// The file path to the base manifest.
			/// </summary>
			public string ManifestA;
			/// <summary>
			/// The file path to the update manifest.
			/// </summary>
			public string ManifestB;
			/// <summary>
			/// The file path to the output manifest.
			/// </summary>
			public string ManifestC;
			/// <summary>
			/// The new version string for the build being produced.
			/// </summary>
			public string BuildVersion;
			/// <summary>
			/// Optional. The set of files that should be kept from ManifestA.
			/// </summary>
			public HashSet<string> FilesToKeepFromA;
			/// <summary>
			/// Optional. The set of files that should be kept from ManifestB.
			/// </summary>
			public HashSet<string> FilesToKeepFromB;
		}

		static BuildPatchToolBase Handler = null;

		public static BuildPatchToolBase Get()
		{
			if (Handler == null)
			{
				Assembly[] LoadedAssemblies = AppDomain.CurrentDomain.GetAssemblies();
				foreach (var Dll in LoadedAssemblies)
				{
					Type[] AllTypes = Dll.GetTypes();
					foreach (var PotentialConfigType in AllTypes)
					{
						if (PotentialConfigType != typeof(BuildPatchToolBase) && typeof(BuildPatchToolBase).IsAssignableFrom(PotentialConfigType))
						{
							Handler = Activator.CreateInstance(PotentialConfigType) as BuildPatchToolBase;
							break;
						}
					}
				}
				if (Handler == null)
				{
					throw new AutomationException("Attempt to use BuildPatchToolBase.Get() and it doesn't appear that there are any modules that implement this class.");
				}
			}
			return Handler;
		}

		/// <summary>
		/// Runs the Build Patch Tool executable to generate patch data using the supplied parameters.
		/// </summary>
		/// <param name="Opts">Parameters which will be passed to the patch tool generation process.</param>
		/// <param name="Version">Which version of BuildPatchTool is desired.</param>
		public abstract void Execute(PatchGenerationOptions Opts, ToolVersion Version = ToolVersion.Live);

		/// <summary>
		/// Runs the Build Patch Tool executable to compactify a cloud directory using the supplied parameters.
		/// </summary>
		/// <param name="Opts">Parameters which will be passed to the patch tool compactify process.</param>
		/// <param name="Version">Which version of BuildPatchTool is desired.</param>
		public abstract void Execute(CompactifyOptions Opts, ToolVersion Version = ToolVersion.Live);

		/// <summary>
		/// Runs the Build Patch Tool executable to enumerate patch data files referenced by a manifest using the supplied parameters.
		/// </summary>
		/// <param name="Opts">Parameters which will be passed to the patch tool enumeration process.</param>
		/// <param name="Version">Which version of BuildPatchTool is desired.</param>
		public abstract void Execute(DataEnumerationOptions Opts, ToolVersion Version = ToolVersion.Live);

		/// <summary>
		/// Runs the Build Patch Tool executable to merge two manifest files producing a hotfix manifest.
		/// </summary>
		/// <param name="Opts">Parameters which will be passed to the patch tool manifest merge process.</param>
		/// <param name="Version">Which version of BuildPatchTool is desired.</param>
		public abstract void Execute(ManifestMergeOptions Opts, ToolVersion Version = ToolVersion.Live);
	}


    /// <summary>
    /// Helper class
    /// </summary>
    public abstract class BuildInfoPublisherBase
    {
        static BuildInfoPublisherBase Handler = null;
 
        public static BuildInfoPublisherBase Get()
        {
            if (Handler == null)
            {
                Assembly[] LoadedAssemblies = AppDomain.CurrentDomain.GetAssemblies();
                foreach (var Dll in LoadedAssemblies)
                {
                    Type[] AllTypes = Dll.GetTypes();
                    foreach (var PotentialConfigType in AllTypes)
                    {
                        if (PotentialConfigType != typeof(BuildInfoPublisherBase) && typeof(BuildInfoPublisherBase).IsAssignableFrom(PotentialConfigType))
                        {
                            Handler = Activator.CreateInstance(PotentialConfigType) as BuildInfoPublisherBase;
                            break;
                        }
                    }
                }
                if (Handler == null)
                {
                    throw new AutomationException("Attempt to use BuildInfoPublisherBase.Get() and it doesn't appear that there are any modules that implement this class.");
                }
            }
            return Handler;
        }

		/// <summary>
		/// Determines whether a given build is registered in build info
		/// </summary>
		/// <param name="StagingInfo">The staging info representing the build to check.</param>
		/// <param name="McpConfigName">Name of which MCP config to check against.</param>
		/// <returns>true if the build is registered, false otherwise</returns>
		abstract public bool BuildExists(BuildPatchToolStagingInfo StagingInfo, string McpConfigName);

        /// <summary>
        /// Given a MCPStagingInfo defining our build info, posts the build to the MCP BuildInfo Service.
        /// </summary>
        /// <param name="stagingInfo">Staging Info describing the BuildInfo to post.</param>
        abstract public void PostBuildInfo(BuildPatchToolStagingInfo stagingInfo);

		/// <summary>
		/// Given a MCPStagingInfo defining our build info and a MCP config name, posts the build to the requested MCP BuildInfo Service.
		/// </summary>
		/// <param name="StagingInfo">Staging Info describing the BuildInfo to post.</param>
		/// <param name="McpConfigName">Name of which MCP config to post to.</param>
		abstract public void PostBuildInfo(BuildPatchToolStagingInfo StagingInfo, string McpConfigName);

		/// <summary>
		/// Given a BuildVersion defining our a build, return the labels applied to that build
		/// </summary>
		/// <param name="BuildVersion">Build version to return labels for.</param>
		/// <param name="McpConfigName">Which BuildInfo backend to get labels from for this promotion attempt.</param>
		abstract public List<string> GetBuildLabels(BuildPatchToolStagingInfo StagingInfo, string McpConfigName);

		/// <summary>
		/// Given a staging info defining our build, return the manifest url for that registered build
		/// </summary>
		/// <param name="StagingInfo">Staging Info describing the BuildInfo to query.</param>
		/// <param name="McpConfigName">Name of which MCP config to query.</param>
		/// <returns></returns>
		abstract public string GetBuildManifestUrl(BuildPatchToolStagingInfo StagingInfo, string McpConfigName);
		/// <summary>
		/// Get a label string for the specific Platform requested.
		/// </summary>
		/// <param name="DestinationLabel">Base of label</param>
		/// <param name="Platform">Platform to add to base label.</param>
		abstract public string GetLabelWithPlatform(string DestinationLabel, MCPPlatform Platform);

		/// <summary>
		/// Get a BuildVersion string with the Platform concatenated on.
		/// </summary>
		/// <param name="DestinationLabel">Base of label</param>
		/// <param name="Platform">Platform to add to base label.</param>
		abstract public string GetBuildVersionWithPlatform(BuildPatchToolStagingInfo StagingInfo);

		/// <summary>
		/// 
		/// </summary>
		/// <param name="AppName">Application name to check the label in</param>
		/// <param name="LabelName">Label name to get the build for</param>
		/// <param name="McpConfigName">Which BuildInfo backend to label the build in.</param>
		/// <returns></returns>
		abstract public string GetLabeledBuildVersion(string AppName, string LabelName, string McpConfigName);

		/// <summary>
		/// Apply the requested label to the requested build in the BuildInfo backend for the requested MCP environment
		/// </summary>
		/// <param name="StagingInfo">Staging info for the build to label.</param>
		/// <param name="DestinationLabelWithPlatform">Label, including platform, to apply</param>
		/// <param name="McpConfigName">Which BuildInfo backend to label the build in.</param>
		abstract public void LabelBuild(BuildPatchToolStagingInfo StagingInfo, string DestinationLabelWithPlatform, string McpConfigName);

        /// <summary>
        /// Informs Patcher Service of a new build availability after async labeling is complete
        /// (this usually means the build was copied to a public file server before the label could be applied).
        /// </summary>
        /// <param name="Command">Parent command</param>
        /// <param name="AppName">Application name that the patcher service will use.</param>
        /// <param name="BuildVersion">BuildVersion string that the patcher service will use.</param>
        /// <param name="ManifestRelativePath">Relative path to the Manifest file relative to the global build root (which is like P:\Builds) </param>
        /// <param name="LabelName">Name of the label that we will be setting.</param>
        abstract public void BuildPromotionCompleted(BuildPatchToolStagingInfo stagingInfo, string AppName, string BuildVersion, string ManifestRelativePath, string PlatformName, string LabelName);
	}

    /// <summary>
    /// Helpers for using the MCP account service
    /// </summary>
    public abstract class McpAccountServiceBase
    {
        static McpAccountServiceBase Handler = null;

        public static McpAccountServiceBase Get()
        {
            if (Handler == null)
            {
                Assembly[] LoadedAssemblies = AppDomain.CurrentDomain.GetAssemblies();
                foreach (var Dll in LoadedAssemblies)
                {
                    Type[] AllTypes = Dll.GetTypes();
                    foreach (var PotentialConfigType in AllTypes)
                    {
                        if (PotentialConfigType != typeof(McpAccountServiceBase) && typeof(McpAccountServiceBase).IsAssignableFrom(PotentialConfigType))
                        {
                            Handler = Activator.CreateInstance(PotentialConfigType) as McpAccountServiceBase;
                            break;
                        }
                    }
                }
                if (Handler == null)
                {
                    throw new AutomationException("Attempt to use McpAccountServiceBase.Get() and it doesn't appear that there are any modules that implement this class.");
                }
            }
            return Handler;
        }
        public abstract string GetClientToken(BuildPatchToolStagingInfo StagingInfo);
		public abstract string GetClientToken(McpConfigData McpConfig);
        public abstract string SendWebRequest(WebRequest Upload, string Method, string ContentType, byte[] Data);
    }

	/// <summary>
	/// Helper class to manage files stored in some arbitrary cloud storage system
	/// </summary>
	public abstract class CloudStorageBase
	{
		private static readonly object LockObj = new object();
		private static Dictionary<string, CloudStorageBase> Handlers = new Dictionary<string, CloudStorageBase>();
		private const string DEFAULT_INSTANCE_NAME = "DefaultInstance";

		/// <summary>
		/// Gets the default instance of CloudStorageBase
		/// </summary>
		/// <returns>A default instance of CloudStorageBase. The first time each instance is returned, it will require initialization with its Init() method.</returns>
		public static CloudStorageBase Get()
		{
			return GetByNameImpl(DEFAULT_INSTANCE_NAME); // Identifier for the default cloud storage
		}

		/// <summary>
		/// Gets an instance of CloudStorageBase.
		/// Multiple calls with the same instance name will return the same object.
		/// </summary>
		/// <param name="InstanceName">The name of the object to return</param>
		/// <returns>An instance of CloudStorageBase. The first time each instance is returned, it will require initialization with its Init() method.</returns>
		public static CloudStorageBase GetByName(string InstanceName)
		{
			if (InstanceName == DEFAULT_INSTANCE_NAME)
			{
				CommandUtils.LogWarning("CloudStorageBase.GetByName called with {0}. This will return the same instance as Get().", DEFAULT_INSTANCE_NAME);
			}
			return GetByNameImpl(InstanceName);
		}

		private  static CloudStorageBase GetByNameImpl(string InstanceName)
		{
			CloudStorageBase Result = null;
			if (!Handlers.TryGetValue(InstanceName, out Result))
			{
				lock (LockObj)
				{
					if (Handlers.ContainsKey(InstanceName))
					{
						Result = Handlers[InstanceName];
					}
					else
					{
						Assembly[] LoadedAssemblies = AppDomain.CurrentDomain.GetAssemblies();
						foreach (var Dll in LoadedAssemblies)
						{
							Type[] AllTypes = Dll.GetTypes();
							foreach (var PotentialConfigType in AllTypes)
							{
								if (PotentialConfigType != typeof(CloudStorageBase) && typeof(CloudStorageBase).IsAssignableFrom(PotentialConfigType))
								{
									Result = Activator.CreateInstance(PotentialConfigType) as CloudStorageBase;
									Handlers.Add(InstanceName, Result);
									break;
								}
							}
						}
					}
				}
				if (Result == null)
				{
					throw new AutomationException("Could not find any modules which provide an implementation of CloudStorageBase.");
				}
			}
			return Result;
		}

		/// <summary>
		/// Initializes the provider.
		/// <param name="Config">Configuration data to initialize the provider. The exact format of the data is provider specific. It might, for example, contain an API key.</param>
		/// </summary>
		abstract public void Init(Dictionary<string,object> Config);

		/// <summary>
		/// Retrieves a file from the cloud storage provider
		/// </summary>
		/// <param name="Container">The name of the folder or container from which contains the file being checked.</param>
		/// <param name="Identifier">The identifier or filename of the file to check.</param>
        	/// <param name="bQuiet">If set to true, all log output for the operation is suppressed.</param>
		/// <returns>True if the file exists in cloud storage, false otherwise.</returns>
		abstract public bool FileExists(string Container, string Identifier, bool bQuiet = false);

		/// <summary>
		/// Retrieves a file from the cloud storage provider
		/// </summary>
		/// <param name="Container">The name of the folder or container from which to retrieve the file.</param>
		/// <param name="Identifier">The identifier or filename of the file to retrieve.</param>
		/// <param name="ContentType">An OUTPUT parameter containing the content's type (null if the cloud provider does not provide this information)</param>
		/// <returns>A byte array containing the file's contents.</returns>
		abstract public byte[] GetFile(string Container, string Identifier, out string ContentType);

		/// <summary>
		/// Posts a file to the cloud storage provider.
		/// </summary>
		/// <param name="Container">The name of the folder or container in which to store the file.</param>
		/// <param name="Identifier">The identifier or filename of the file to write.</param>
		/// <param name="Contents">A byte array containing the data to write.</param>
		/// <param name="ContentType">The MIME type of the file being uploaded. If left NULL, will be determined server-side by cloud provider.</param>
		/// <param name="bOverwrite">If true, will overwrite an existing file.  If false, will throw an exception if the file exists.</param>
		/// <param name="bMakePublic">Specifies whether the file should be made publicly readable.</param>
        /// <param name="bQuiet">If set to true, all log output for the operation is supressed.</param>
		/// <returns>A PostFileResult indicating whether the call was successful, and the URL to the uploaded file.</returns>
		public PostFileResult PostFile(string Container, string Identifier, byte[] Contents, string ContentType = null, bool bOverwrite = true, bool bMakePublic = false, bool bQuiet = false)
		{
			return PostFileAsync(Container, Identifier, Contents, ContentType, bOverwrite, bMakePublic, bQuiet).Result;
		}

		/// <summary>
		/// Posts a file to the cloud storage provider asynchronously.
		/// </summary>
		/// <param name="Container">The name of the folder or container in which to store the file.</param>
		/// <param name="Identifier">The identifier or filename of the file to write.</param>
		/// <param name="Contents">A byte array containing the data to write.</param>
		/// <param name="ContentType">The MIME type of the file being uploaded. If left NULL, will be determined server-side by cloud provider.</param>
		/// <param name="bOverwrite">If true, will overwrite an existing file.  If false, will throw an exception if the file exists.</param>
		/// <param name="bMakePublic">Specifies whether the file should be made publicly readable.</param>
        /// <param name="bQuiet">If set to true, all log output for the operation is supressed.</param>
		/// <returns>A PostFileResult indicating whether the call was successful, and the URL to the uploaded file.</returns>
		abstract public Task<PostFileResult> PostFileAsync(string Container, string Identifier, byte[] Contents, string ContentType = null, bool bOverwrite = true, bool bMakePublic = false, bool bQuiet = false);

		/// <summary>
		/// Posts a file to the cloud storage provider.
		/// </summary>
		/// <param name="Container">The name of the folder or container in which to store the file.</param>
		/// <param name="Identifier">The identifier or filename of the file to write.</param>
		/// <param name="SourceFilePath">The full path of the file to upload.</param>
		/// <param name="ContentType">The MIME type of the file being uploaded. If left NULL, will be determined server-side by cloud provider.</param>
		/// <param name="bOverwrite">If true, will overwrite an existing file.  If false, will throw an exception if the file exists.</param>
		/// <param name="bMakePublic">Specifies whether the file should be made publicly readable.</param>
        /// <param name="bQuiet">If set to true, all log output for the operation is supressed.</param>
		/// <returns>A PostFileResult indicating whether the call was successful, and the URL to the uploaded file.</returns>
		public PostFileResult PostFile(string Container, string Identifier, string SourceFilePath, string ContentType = null, bool bOverwrite = true, bool bMakePublic = false, bool bQuiet = false)
		{
			return PostFileAsync(Container, Identifier, SourceFilePath, ContentType, bOverwrite, bMakePublic, bQuiet).Result;
		}

		/// <summary>
		/// Posts a file to the cloud storage provider asynchronously.
		/// </summary>
		/// <param name="Container">The name of the folder or container in which to store the file.</param>
		/// <param name="Identifier">The identifier or filename of the file to write.</param>
		/// <param name="SourceFilePath">The full path of the file to upload.</param>
		/// <param name="ContentType">The MIME type of the file being uploaded. If left NULL, will be determined server-side by cloud provider.</param>
		/// <param name="bOverwrite">If true, will overwrite an existing file.  If false, will throw an exception if the file exists.</param>
		/// <param name="bMakePublic">Specifies whether the file should be made publicly readable.</param>
        /// <param name="bQuiet">If set to true, all log output for the operation is supressed.</param>
		/// <returns>A PostFileResult indicating whether the call was successful, and the URL to the uploaded file.</returns>
		abstract public Task<PostFileResult> PostFileAsync(string Container, string Identifier, string SourceFilePath, string ContentType = null, bool bOverwrite = true, bool bMakePublic = false, bool bQuiet = false);

		/// <summary>
		/// Posts a file to the cloud storage provider using multiple connections.
		/// </summary>
		/// <param name="Container">The name of the folder or container in which to store the file.</param>
		/// <param name="Identifier">The identifier or filename of the file to write.</param>
		/// <param name="SourceFilePath">The full path of the file to upload.</param>
		/// <param name="NumConcurrentConnections">The number of concurrent connections to use during uploading.</param>
		/// <param name="PartSizeMegabytes">The size of each part that is uploaded. Minimum (and default) is 5 MB.</param>
		/// <param name="ContentType">The MIME type of the file being uploaded. If left NULL, will be determined server-side by cloud provider.</param>
		/// <param name="bOverwrite">If true, will overwrite an existing file. If false, will throw an exception if the file exists.</param>
		/// <param name="bMakePublic">Specifies whether the file should be made publicly readable.</param>
		/// <param name="bQuiet">If set to true, all log output for the operation is supressed.</param>
		/// <returns>A PostFileResult indicating whether the call was successful, and the URL to the uploaded file.</returns>
		public PostFileResult PostMultipartFile(string Container, string Identifier, string SourceFilePath, int NumConcurrentConnections, decimal PartSizeMegabytes = 5.0m, string ContentType = null, bool bOverwrite = true, bool bMakePublic = false, bool bQuiet = false)
		{
			return PostMultipartFileAsync(Container, Identifier, SourceFilePath, NumConcurrentConnections, PartSizeMegabytes, ContentType, bOverwrite, bMakePublic, bQuiet).Result;
		}

		/// <summary>
		/// Posts a file to the cloud storage provider using multiple connections asynchronously.
		/// </summary>
		/// <param name="Container">The name of the folder or container in which to store the file.</param>
		/// <param name="Identifier">The identifier or filename of the file to write.</param>
		/// <param name="SourceFilePath">The full path of the file to upload.</param>
		/// <param name="NumConcurrentConnections">The number of concurrent connections to use during uploading.</param>
		/// <param name="PartSizeMegabytes">The size of each part that is uploaded. Minimum (and default) is 5 MB.</param>
		/// <param name="ContentType">The MIME type of the file being uploaded. If left NULL, will be determined server-side by cloud provider.</param>
		/// <param name="bOverwrite">If true, will overwrite an existing file. If false, will throw an exception if the file exists.</param>
		/// <param name="bMakePublic">Specifies whether the file should be made publicly readable.</param>
		/// <param name="bQuiet">If set to true, all log output for the operation is supressed.</param>
		/// <returns>A PostFileResult indicating whether the call was successful, and the URL to the uploaded file.</returns>
		abstract public Task<PostFileResult> PostMultipartFileAsync(string Container, string Identifier, string SourceFilePath, int NumConcurrentConnections, decimal PartSizeMegabytes = 5.0m, string ContentType = null, bool bOverwrite = true, bool bMakePublic = false, bool bQuiet = false);

		/// <summary>
		/// Deletes a file from cloud storage
		/// </summary>
		/// <param name="Container">The name of the folder or container in which to store the file.</param>
		/// <param name="Identifier">The identifier or filename of the file to write.</param>
		abstract public void DeleteFile(string Container, string Identifier);

		/// <summary>
		/// Deletes a folder from cloud storage
		/// </summary>
		/// <param name="Container">The name of the folder or container from which to delete the file.</param>
		/// <param name="FolderIdentifier">The identifier or name of the folder to delete.</param>
		abstract public void DeleteFolder(string Container, string FolderIdentifier);

		/// <summary>
		/// Retrieves a list of folders from the cloud storage provider
		/// </summary>
		/// <param name="Container">The name of the container from which to list folders.</param>
		/// <param name="Prefix">A string to specify the identifer that you want to list from. Typically used to specify a relative folder within the container to list all of its folders. Specify null to return folders in the root of the container.</param>
		/// <param name="Options">An action which acts upon an options object to configure the operation. See ListOptions for more details.</param>
		/// <returns>An array of paths to the folders in the specified container and matching the prefix constraint.</returns>
		public string[] ListFolders(string Container, string Prefix, Action<ListOptions> Options)
		{
			ListOptions Opts = new ListOptions();
			if (Options != null)
			{
				Options(Opts);
			}
			return ListFolders(Container, Prefix, Opts);
		}

		/// <summary>
		/// Retrieves a list of folders from the cloud storage provider
		/// </summary>
		/// <param name="Container">The name of the container from which to list folders.</param>
		/// <param name="Prefix">A string to specify the identifer that you want to list from. Typically used to specify a relative folder within the container to list all of its folders. Specify null to return folders in the root of the container.</param>
		/// <param name="Options">An options object to configure the operation. See ListOptions for more details.</param>
		/// <returns>An array of paths to the folders in the specified container and matching the prefix constraint.</returns>
		abstract public string[] ListFolders(string Container, string Prefix, ListOptions Options);

		/// <summary>
		/// DEPRECATED. Retrieves a list of files from the cloud storage provider.  See overload with ListOptions for non-deprecated use.
		/// </summary>
		/// <param name="Container">The name of the folder or container from which to list files.</param>
		/// <param name="Prefix">A string with which the identifier or filename should start. Typically used to specify a relative directory within the container to list all of its files recursively. Specify null to return all files.</param>
		/// <param name="Recursive">Indicates whether the list of files returned should traverse subdirectories</param>
		/// <param name="bQuiet">If set to true, all log output for the operation is supressed.</param>
		/// <returns>An array of paths to the files in the specified location and matching the prefix constraint.</returns>
		public string[] ListFiles(string Container, string Prefix = null, bool bRecursive = true, bool bQuiet = false)
		{
			return ListFiles(Container, Prefix, opts =>
			{
				opts.bRecursive = bRecursive;
				opts.bQuiet = bQuiet;
			});
		}

		/// <summary>
		/// Retrieves a list of files from the cloud storage provider
		/// </summary>
		/// <param name="Container">The name of the container from which to list folders.</param>
		/// <param name="Prefix">A string to specify the identifer that you want to list from. Typically used to specify a relative folder within the container to list all of its folders. Specify null to return folders in the root of the container.</param>
		/// <param name="Options">An action which acts upon an options object to configure the operation. See ListOptions for more details.</param>
		/// <returns>An array of paths to the folders in the specified container and matching the prefix constraint.</returns>
		public string[] ListFiles(string Container, string Prefix, Action<ListOptions> Options)
		{
			ListOptions Opts = new ListOptions();
			if (Options != null)
			{
				Options(Opts);
			}
			return ListFiles(Container, Prefix, Opts);
		}

		/// <summary>
		/// Retrieves a list of files from the cloud storage provider.
		/// </summary>
		/// <param name="Container">The name of the folder or container from which to list files.</param>
		/// <param name="Prefix">A string with which the identifier or filename should start. Typically used to specify a relative directory within the container to list all of its files recursively. Specify null to return all files.</param>
		/// <param name="Options">An options object to configure the operation. See ListOptions for more details.</param>
		/// <returns>An array of paths to the files in the specified location and matching the prefix constraint.</returns>
		abstract public string[] ListFiles(string Container, string Prefix, ListOptions Options);

		/// <summary>
		/// Sets one or more items of metadata on an object in cloud storage
		/// </summary>
		/// <param name="Container">The name of the folder or container in which the file is stored.</param>
		/// <param name="Identifier">The identifier of filename of the file to set metadata on.</param>
		/// <param name="Metadata">A dictionary containing the metadata keys and their values</param>
		/// <param name="bMerge">If true, then existing metadata will be replaced (or overwritten if the keys match). If false, no existing metadata is retained.</param>
		abstract public void SetMetadata(string Container, string Identifier, IDictionary<string, object> Metadata, bool bMerge = true);

		/// <summary>
		/// Gets all items of metadata on an object in cloud storage. Metadata values are all returned as strings.
		/// </summary>
		/// <param name="Container">The name of the folder or container in which the file is stored.</param>
		/// <param name="Identifier">The identifier of filename of the file to get metadata.</param>
		abstract public Dictionary<string, string> GetMetadata(string Container, string Identifier);

		/// <summary>
		/// Gets an item of metadata from an object in cloud storage. The object is casted to the specified type.
		/// </summary>
		/// <param name="Container">The name of the folder or container in which the file is stored.</param>
		/// <param name="Identifier">The identifier of filename of the file to get metadata.</param>
		/// <param name="MetadataKey">The key of the item of metadata to retrieve.</param>
		abstract public T GetMetadata<T>(string Container, string Identifier, string MetadataKey);

		/// <summary>
		/// Updates the timestamp on a particular file in cloud storage to the current time.
		/// </summary>
		/// <param name="Container">The name of the container in which the file is stored.</param>
		/// <param name="Identifier">The identifier of filename of the file to touch.</param>
		abstract public void TouchFile(string Container, string Identifier);

		/// <summary>
		/// Copies manifest and chunks from a staged location to cloud storage.
		/// </summary>
		/// <param name="Container">The name of the container in which to store files.</param>
		/// <param name="stagingInfo">Staging info used to determine where the chunks are to copy.</param>
		abstract public void CopyChunksToCloudStorage(string Container, BuildPatchToolStagingInfo StagingInfo);

		/// <summary>
		/// Copies manifest and its chunks from a specific path to a given target folder in the cloud.
		/// </summary>
		/// <param name="Container">The name of the container in which to store files.</param>
		/// <param name="RemoteCloudDir">The path within the container that the files should be stored in.</param>
		/// <param name="ManifestFilePath">The full path of the manifest file to copy.</param>
		abstract public void CopyChunksToCloudStorage(string Container, string RemoteCloudDir, string ManifestFilePath);

		/// <summary>
		/// Verifies whether a manifest for a given build is in cloud storage.
		/// </summary>
		/// <param name="Container">The name of the folder or container in which to store files.</param>
		/// <param name="stagingInfo">Staging info representing the build to check.</param>
		/// <returns>True if the manifest exists in cloud storage, false otherwise.</returns>
		abstract public bool IsManifestOnCloudStorage(string Container, BuildPatchToolStagingInfo StagingInfo);

		public class PostFileResult
		{
			/// <summary>
			/// Set to the URL of the uploaded file on success
			/// </summary>
			public string ObjectURL { get; set; }

			/// <summary>
			/// Set to true if the write succeeds, false otherwise.
			/// </summary>
			public bool bSuccess { get; set; }
		}

		/// <summary>
		/// Encapsulates options used when listing files or folders using ListFiles and ListFolders
		/// </summary>
		public class ListOptions
		{
			public ListOptions()
			{
				bQuiet = false;
				bRecursive = false;
				bReturnURLs = true;
			}

			/// <summary>
			/// If set to true, all log output for the operation is suppressed. Defaults to false.
			/// </summary>
			public bool bQuiet { get; set; }

			/// <summary>
			/// Indicates whether the list of files returned should traverse subfolders. Defaults to false.
			/// </summary>
			public bool bRecursive { get; set; }

			/// <summary>
			/// If true, returns the full URL to the listed objects. If false, returns their identifier within the container. Defaults to true.
			/// </summary>
			public bool bReturnURLs { get; set; }
		}
	}
}

namespace EpicGames.MCP.Config
{
    /// <summary>
    /// Class for retrieving MCP configuration data
    /// </summary>
    public class McpConfigHelper
    {
        // List of configs is cached off for fetching from multiple times
        private static Dictionary<string, McpConfigData> Configs;

        public static McpConfigData Find(string ConfigName)
        {
            if (Configs == null)
            {
                // Load all secret configs by trying to instantiate all classes derived from McpConfig from all loaded DLLs.
                // Note that we're using the default constructor on the secret config types.
                Configs = new Dictionary<string, McpConfigData>();
                Assembly[] LoadedAssemblies = AppDomain.CurrentDomain.GetAssemblies();
                foreach (var Dll in LoadedAssemblies)
                {
                    Type[] AllTypes = Dll.GetTypes();
                    foreach (var PotentialConfigType in AllTypes)
                    {
                        if (PotentialConfigType != typeof(McpConfigData) && typeof(McpConfigData).IsAssignableFrom(PotentialConfigType))
                        {
                            try
                            {
                                McpConfigData Config = Activator.CreateInstance(PotentialConfigType) as McpConfigData;
                                if (Config != null)
                                {
                                    Configs.Add(Config.Name, Config);
                                }
                            }
                            catch
                            {
                                BuildCommand.LogWarning("Unable to create McpConfig: {0}", PotentialConfigType.Name);
                            }
                        }
                    }
                }
            }
            McpConfigData LoadedConfig;
            Configs.TryGetValue(ConfigName, out LoadedConfig);
            if (LoadedConfig == null)
            {
                throw new AutomationException("Unable to find requested McpConfig: {0}", ConfigName);
            }
            return LoadedConfig;
        }
    }

    // Class for storing mcp configuration data
    public class McpConfigData
    {
		public McpConfigData(string InName, string InAccountBaseUrl, string InFortniteBaseUrl, string InLauncherBaseUrl, string InBuildInfoV2BaseUrl, string InLauncherV2BaseUrl, string InClientId, string InClientSecret)
        {
            Name = InName;
            AccountBaseUrl = InAccountBaseUrl;
            FortniteBaseUrl = InFortniteBaseUrl;
            LauncherBaseUrl = InLauncherBaseUrl;
			BuildInfoV2BaseUrl = InBuildInfoV2BaseUrl;
			LauncherV2BaseUrl = InLauncherV2BaseUrl;
            ClientId = InClientId;
            ClientSecret = InClientSecret;
        }

        public string Name;
        public string AccountBaseUrl;
        public string FortniteBaseUrl;
        public string LauncherBaseUrl;
		public string BuildInfoV2BaseUrl;
		public string LauncherV2BaseUrl;
        public string ClientId;
        public string ClientSecret;

        public void SpewValues()
        {
            CommandUtils.LogVerbose("Name : {0}", Name);
            CommandUtils.LogVerbose("AccountBaseUrl : {0}", AccountBaseUrl);
            CommandUtils.LogVerbose("FortniteBaseUrl : {0}", FortniteBaseUrl);
            CommandUtils.LogVerbose("LauncherBaseUrl : {0}", LauncherBaseUrl);
			CommandUtils.LogVerbose("BuildInfoV2BaseUrl : {0}", BuildInfoV2BaseUrl);
			CommandUtils.LogVerbose("LauncherV2BaseUrl : {0}", LauncherV2BaseUrl);
            CommandUtils.LogVerbose("ClientId : {0}", ClientId);
            // we don't really want this in logs CommandUtils.Log("ClientSecret : {0}", ClientSecret);
        }
    }

    public class McpConfigMapper
    {
        static public McpConfigData FromMcpConfigKey(string McpConfigKey)
        {
            return McpConfigHelper.Find("MainGameDevNet");
        }

        static public McpConfigData FromStagingInfo(EpicGames.MCP.Automation.BuildPatchToolStagingInfo StagingInfo)
        {
            string McpConfigNameToLookup = null;
            if (StagingInfo.OwnerCommand != null)
            {
                McpConfigNameToLookup = StagingInfo.OwnerCommand.ParseParamValue("MCPConfig");
            }
            if (String.IsNullOrEmpty(McpConfigNameToLookup))
            {
                return FromMcpConfigKey(StagingInfo.McpConfigKey);
            }
            return McpConfigHelper.Find(McpConfigNameToLookup);
        }
    }

}
