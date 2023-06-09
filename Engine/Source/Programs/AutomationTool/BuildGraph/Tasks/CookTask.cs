﻿using AutomationTool;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Xml;
using UnrealBuildTool;

namespace BuildGraph.Tasks
{
	/// <summary>
	/// Parameters for a task that runs the cooker
	/// </summary>
	public class CookTaskParameters
	{
		/// <summary>
		/// Project file to be cooked
		/// </summary>
		[TaskParameter]
		public string Project;

		/// <summary>
		/// The cook platform to target (eg. WindowsNoEditor)
		/// </summary>
		[TaskParameter]
		public string Platform;

		/// <summary>
		/// List of maps to be cooked, separated by '+' characters
		/// </summary>
		[TaskParameter(Optional = true)]
		public string Maps;

		/// <summary>
		/// Additional arguments to be passed to the cooker
		/// </summary>
		[TaskParameter(Optional = true)]
		public bool Versioned = false;
	
		/// <summary>
		/// Additional arguments to be passed to the cooker
		/// </summary>
		[TaskParameter(Optional = true)]
		public string Arguments = "";

		/// <summary>
		/// Tag to be applied to build products of this task
		/// </summary>
		[TaskParameter(Optional = true, ValidationType = TaskParameterValidationType.TagList)]
		public string Tag;
	}

	/// <summary>
	/// Cook a selection of maps for a certain platform
	/// </summary>
	[TaskElement("Cook", typeof(CookTaskParameters))]
	public class CookTask : CustomTask
	{
		/// <summary>
		/// Parameters for the task
		/// </summary>
		CookTaskParameters Parameters;

		/// <summary>
		/// Constructor.
		/// </summary>
		/// <param name="InParameters">Parameters for this task</param>
		public CookTask(CookTaskParameters InParameters)
		{
			Parameters = InParameters;
		}

		/// <summary>
		/// Execute the task.
		/// </summary>
		/// <param name="Job">Information about the current job</param>
		/// <param name="BuildProducts">Set of build products produced by this node.</param>
		/// <param name="TagNameToFileSet">Mapping from tag names to the set of files they include</param>
		/// <returns>True if the task succeeded</returns>
		public override bool Execute(JobContext Job, HashSet<FileReference> BuildProducts, Dictionary<string, HashSet<FileReference>> TagNameToFileSet)
		{
			// Figure out the project that this target belongs to
			FileReference ProjectFile = null;
			if(Parameters.Project != null)
			{
				ProjectFile = new FileReference(Parameters.Project);
				if(!ProjectFile.Exists())
				{
					CommandUtils.LogError("Missing project file - {0}", ProjectFile.FullName);
					return false;
				}
			}

			// Execute the cooker
			using(TelemetryStopwatch CookStopwatch = new TelemetryStopwatch("Cook.{0}.{1}", (ProjectFile == null)? "UE4" : ProjectFile.GetFileNameWithoutExtension(), Parameters.Platform))
			{
				string[] Maps = (Parameters.Maps == null)? null : Parameters.Maps.Split(new char[]{ '+' });
				string Arguments = (Parameters.Versioned ? "" : "-Unversioned ") + "-LogCmds=\"LogSavePackage Warning\" " + Parameters.Arguments;
				CommandUtils.CookCommandlet(ProjectFile, "UE4Editor-Cmd.exe", Maps, null, null, null, Parameters.Platform, Arguments);
			}

			// Find all the cooked files
			DirectoryReference CookedDirectory = DirectoryReference.Combine(ProjectFile.Directory, "Saved", "Cooked", Parameters.Platform);
			if(!CookedDirectory.Exists())
			{
				CommandUtils.LogError("Cook output directory not found ({0})", CookedDirectory.FullName);
				return false;
			}
			List<FileReference> CookedFiles = CookedDirectory.EnumerateFileReferences("*", System.IO.SearchOption.AllDirectories).ToList();
			if(CookedFiles.Count == 0)
			{
				CommandUtils.LogError("Cooking did not produce any files in {0}", CookedDirectory.FullName);
				return false;
			}

			// Apply the optional tag to the build products
			foreach(string TagName in FindTagNamesFromList(Parameters.Tag))
			{
				FindOrAddTagSet(TagNameToFileSet, TagName).UnionWith(CookedFiles);
			}

			// Add them to the set of build products
			BuildProducts.UnionWith(CookedFiles);
			return true;
		}

		/// <summary>
		/// Output this task out to an XML writer.
		/// </summary>
		public override void Write(XmlWriter Writer)
		{
			Write(Writer, Parameters);
		}

		/// <summary>
		/// Find all the tags which are used as inputs to this task
		/// </summary>
		/// <returns>The tag names which are read by this task</returns>
		public override IEnumerable<string> FindConsumedTagNames()
		{
			yield break;
		}

		/// <summary>
		/// Find all the tags which are modified by this task
		/// </summary>
		/// <returns>The tag names which are modified by this task</returns>
		public override IEnumerable<string> FindProducedTagNames()
		{
			return FindTagNamesFromList(Parameters.Tag);
		}
	}
}
