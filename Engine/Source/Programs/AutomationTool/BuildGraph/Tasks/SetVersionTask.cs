﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Xml;
using UnrealBuildTool;

namespace AutomationTool.Tasks
{
	/// <summary>
	/// Parameters for the version task
	/// </summary>
	public class SetVersionTaskParameters
	{
		/// <summary>
		/// The changelist to set in the version files
		/// </summary>
		[TaskParameter]
		public int Change;

		/// <summary>
		/// The branch string
		/// </summary>
		[TaskParameter]
		public string Branch;

		/// <summary>
		/// The build version string
		/// </summary>
		[TaskParameter(Optional = true)]
		public string Build;

		/// <summary>
		/// Whether to set the IS_LICENSEE_VERSION flag to true
		/// </summary>
		[TaskParameter(Optional = true)]
		public bool Licensee;

		/// <summary>
		/// If set, don't actually write to the files - just return the version files that would be updated. Useful for local builds.
		/// </summary>
		[TaskParameter(Optional = true)]
		public bool SkipWrite;

		/// <summary>
		/// Tag to be applied to build products of this task
		/// </summary>
		[TaskParameter(Optional = true, ValidationType = TaskParameterValidationType.TagList)]
		public string Tag;
	}

	/// <summary>
	/// Task which updates the version files in the current branch
	/// </summary>
	[TaskElement("SetVersion", typeof(SetVersionTaskParameters))]
	public class SetVersionTask : CustomTask
	{
		/// <summary>
		/// Parameters for the task
		/// </summary>
		SetVersionTaskParameters Parameters;

		/// <summary>
		/// Construct a version task
		/// </summary>
		/// <param name="InParameters">Parameters for this task</param>
		public SetVersionTask(SetVersionTaskParameters InParameters)
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
			// Update the version files
			List<string> FileNames = UE4Build.StaticUpdateVersionFiles(Parameters.Change, Parameters.Branch, Parameters.Build, Parameters.Licensee, !Parameters.SkipWrite);
			List<FileReference> VersionFiles = FileNames.Select(x => new FileReference(x)).ToList();

			// Apply the optional tag to them
			foreach(string TagName in FindTagNamesFromList(Parameters.Tag))
			{
				FindOrAddTagSet(TagNameToFileSet, TagName).UnionWith(VersionFiles);
			}

			// Add them to the list of build products
			BuildProducts.UnionWith(VersionFiles);
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
