﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using UnrealBuildTool;
using AutomationTool;
using System.Xml;

namespace AutomationTool
{
	/// <summary>
	/// Reference to an output tag from a particular node
	/// </summary>
	class NodeOutput
	{
		/// <summary>
		/// The node which produces the given output
		/// </summary>
		public Node ProducingNode;

		/// <summary>
		/// Name of the tag
		/// </summary>
		public string TagName;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="InProducingNode">Node which produces the given output</param>
		/// <param name="InTagName">Name of the tag</param>
		public NodeOutput(Node InProducingNode, string InTagName)
		{
			ProducingNode = InProducingNode;
			TagName = InTagName;
		}

		/// <summary>
		/// Returns a string representation of this output for debugging purposes
		/// </summary>
		/// <returns>The name of this output</returns>
		public override string ToString()
		{
			return String.Format("{0}: {1}", ProducingNode.Name, TagName);
		}
	}

	/// <summary>
	/// Defines a node, a container for tasks and the smallest unit of execution that can be run as part of a build graph.
	/// </summary>
	class Node
	{
		/// <summary>
		/// The node's name
		/// </summary>
		public string Name;

		/// <summary>
		/// Array of inputs which this node requires to run
		/// </summary>
		public NodeOutput[] Inputs;

		/// <summary>
		/// Array of outputs produced by this node
		/// </summary>
		public NodeOutput[] Outputs;

		/// <summary>
		/// Nodes which this node has input dependencies on
		/// </summary>
		public Node[] InputDependencies;

		/// <summary>
		/// Nodes which this node needs to run after
		/// </summary>
		public Node[] OrderDependencies;

		/// <summary>
		/// The trigger which controls whether this node will be executed
		/// </summary>
		public ManualTrigger ControllingTrigger;

		/// <summary>
		/// Tokens which must be acquired for this node to run
		/// </summary>
		public FileReference[] RequiredTokens;

		/// <summary>
		/// List of tasks to execute
		/// </summary>
		public List<CustomTask> Tasks = new List<CustomTask>();

		/// <summary>
		/// List of email addresses to notify if this node fails.
		/// </summary>
		public HashSet<string> NotifyUsers = new HashSet<string>(StringComparer.InvariantCultureIgnoreCase);

		/// <summary>
		/// If set, anyone that has submitted to one of the given paths will be notified on failure of this node
		/// </summary>
		public HashSet<string> NotifySubmitters = new HashSet<string>(StringComparer.InvariantCultureIgnoreCase);

		/// <summary>
		/// Whether to ignore warnings produced by this node
		/// </summary>
		public bool bNotifyOnWarnings = true;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="InName">The name of this node</param>
		/// <param name="InInputs">Inputs that this node depends on</param>
		/// <param name="InOutputNames">Names of the outputs that this node produces</param>
		/// <param name="InInputDependencies">Nodes which this node is dependent on for its inputs</param>
		/// <param name="InOrderDependencies">Nodes which this node needs to run after. Should include all input dependencies.</param>
		/// <param name="InControllingTrigger">The trigger which this node is behind</param>
		/// <param name="InRequiredTokens">Optional tokens which must be required for this node to run</param>
		public Node(string InName, NodeOutput[] InInputs, string[] InOutputNames, Node[] InInputDependencies, Node[] InOrderDependencies, ManualTrigger InControllingTrigger, FileReference[] InRequiredTokens)
		{
			Name = InName;
			Inputs = InInputs;

			List<NodeOutput> AllOutputs = new List<NodeOutput>();
			AllOutputs.Add(new NodeOutput(this, "#" + Name));
			AllOutputs.AddRange(InOutputNames.Where(x => String.Compare(x, Name, StringComparison.InvariantCultureIgnoreCase) != 0).Select(x => new NodeOutput(this, x)));
			Outputs = AllOutputs.ToArray();

			InputDependencies = InInputDependencies;
			OrderDependencies = InOrderDependencies;
			ControllingTrigger = InControllingTrigger;
			RequiredTokens = InRequiredTokens;
		}

		/// <summary>
		/// Returns the default output for this node, which includes all build products
		/// </summary>
		public NodeOutput DefaultOutput
		{
			get { return Outputs[0]; }
		}

		/// <summary>
		/// Build all the tasks for this node
		/// </summary>
		/// <param name="Job">Information about the current job</param>
		/// <param name="BuildProducts">Set of build products produced by this node.</param>
		/// <param name="TagNameToFileSet">Mapping from tag names to the set of files they include. Should be set to contain the node inputs on entry.</param>
		/// <returns>Whether the task succeeded or not. Exiting with an exception will be caught and treated as a failure.</returns>
		public bool Build(JobContext Job, Dictionary<string, HashSet<FileReference>> TagNameToFileSet)
		{
			// Allow tasks to merge together
			MergeTasks();

			// Build everything
			HashSet<FileReference> BuildProducts = TagNameToFileSet[DefaultOutput.TagName];
			foreach(CustomTask Task in Tasks)
			{
				if(!Task.Execute(Job, BuildProducts, TagNameToFileSet))
				{
					CommandUtils.Log("Failed to execute task.");
					return false;
				}
			}

			// Remove anything that doesn't exist, since these files weren't explicitly tagged
			BuildProducts.RemoveWhere(x => !x.Exists());
			return true;
		}

		/// <summary>
		/// Merge tasks which can be combined together
		/// </summary>
		void MergeTasks()
		{
			List<CustomTask> MergedTasks = new List<CustomTask>();
			while(Tasks.Count > 0)
			{
				CustomTask NextTask = Tasks[0];
				Tasks.RemoveAt(0);
				NextTask.Merge(Tasks);
				MergedTasks.Add(NextTask);
			}
			Tasks = MergedTasks;
		}

		/// <summary>
		/// Determines the minimal set of direct input dependencies for this node to run
		/// </summary>
		/// <returns>Sequence of nodes that are direct inputs to this node</returns>
		public IEnumerable<Node> GetDirectInputDependencies()
		{
			HashSet<Node> DirectDependencies = new HashSet<Node>(InputDependencies);
			foreach(Node InputDependency in InputDependencies)
			{
				DirectDependencies.ExceptWith(InputDependency.InputDependencies);
			}
			return DirectDependencies;
		}

		/// <summary>
		/// Determines the minimal set of direct order dependencies for this node to run
		/// </summary>
		/// <returns>Sequence of nodes that are direct order dependencies of this node</returns>
		public IEnumerable<Node> GetDirectOrderDependencies()
		{
			HashSet<Node> DirectDependencies = new HashSet<Node>(OrderDependencies);
			foreach(Node OrderDependency in OrderDependencies)
			{
				DirectDependencies.ExceptWith(OrderDependency.OrderDependencies);
			}
			return DirectDependencies;
		}

		/// <summary>
		/// Checks whether this node is behind the given trigger
		/// </summary>
		/// <param name="Trigger">The trigger to check</param>
		/// <returns>True if the node is directly or indirectly behind the given trigger, false otherwise</returns>
		public bool IsBehind(ManualTrigger Trigger)
		{
			for(ManualTrigger OtherTrigger = ControllingTrigger; OtherTrigger != Trigger; OtherTrigger = OtherTrigger.Parent)
			{
				if(OtherTrigger == null)
				{
					return false;
				}
			}
			return true;
		}

		/// <summary>
		/// Write this node to an XML writer
		/// </summary>
		/// <param name="Writer">The writer to output the node to</param>
		public void Write(XmlWriter Writer)
		{
			Writer.WriteStartElement("Node");
			Writer.WriteAttributeString("Name", Name);

			string[] RequireNames = Inputs.Select(x => x.TagName).ToArray();
			if (RequireNames.Length > 0)
			{
				Writer.WriteAttributeString("Requires", String.Join(";", RequireNames));
			}

			string[] ProducesNames = Outputs.Where(x => x != DefaultOutput).Select(x => x.TagName).ToArray();
			if (ProducesNames.Length > 0)
			{
				Writer.WriteAttributeString("Produces", String.Join(";", ProducesNames));
			}

			string[] AfterNames = GetDirectOrderDependencies().Except(InputDependencies).Select(x => x.Name).ToArray();
			if (AfterNames.Length > 0)
			{
				Writer.WriteAttributeString("After", String.Join(";", AfterNames));
			}

			if (!bNotifyOnWarnings)
			{
				Writer.WriteAttributeString("NotifyOnWarnings", bNotifyOnWarnings.ToString());
			}

			foreach (CustomTask Task in Tasks)
			{
				Task.Write(Writer);
			}
			Writer.WriteEndElement();
		}

		/// <summary>
		/// Returns the name of this node
		/// </summary>
		/// <returns>The name of this node</returns>
		public override string ToString()
		{
			return Name;
		}
	}
}
